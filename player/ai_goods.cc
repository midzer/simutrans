
/* standard good AI code */

#include "../simtypes.h"

#include "simplay.h"

#include "../simhalt.h"
#include "../simtools.h"
#include "../simworld.h"

#include "../bauer/brueckenbauer.h"
#include "../bauer/hausbauer.h"
#include "../bauer/tunnelbauer.h"
#include "../bauer/vehikelbauer.h"
#include "../bauer/wegbauer.h"

#include "../dataobj/loadsave.h"

#include "../dings/wayobj.h"

#include "../utils/simstring.h"

#include "../vehicle/simvehikel.h"

#include "ai_goods.h"


ai_goods_t::ai_goods_t(karte_t *wl, uint8 nr) : ai_t(wl,nr)
{
	state = NR_INIT;

	root = NULL;
	start = NULL;
	ziel = NULL;

	count = 0;

	rail_engine = NULL;
	rail_vehicle = NULL;
	rail_weg = NULL;
	road_vehicle = NULL;
	ship_vehicle = NULL;
	road_weg = NULL;

	next_contruction_steps = welt->gib_steps()+simrand(400);

	road_transport = nr<7;
	rail_transport = nr>2;
	ship_transport = true;
}




/**
 * Methode fuer jaehrliche Aktionen
 * @author Hj. Malthaner, Owen Rudge, hsiegeln
 */
void ai_goods_t::neues_jahr()
{
	spieler_t::neues_jahr();

	// AI will reconsider the oldes unbuiltable lines again
	uint remove = (uint)max(0,(int)forbidden_conections.count()-3);
	while(  remove < forbidden_conections.count()  ) {
		forbidden_conections.remove_first();
	}
}



void ai_goods_t::rotate90( const sint16 y_size )
{
	spieler_t::rotate90( y_size );

	// rotate places
	platz1.rotate90( y_size );
	platz2.rotate90( y_size );
	size1.rotate90( 0 );
	size2.rotate90( 0 );
}



/* Activates/deactivates a player
 * @author prissi
 */
bool ai_goods_t::set_active(bool new_state)
{
	// something to change?
	if(automat!=new_state) {

		if(!new_state) {
			// deactivate AI
			automat = false;
			state = NR_INIT;
			start = ziel = NULL;
		}
		else {
			// aktivate AI
			automat = true;
		}
	}
	return automat;
}



/* recursive lookup of a factory tree:
 * sets start and ziel to the next needed supplier
 * start always with the first branch, if there are more goods
 */
bool ai_goods_t::get_factory_tree_lowest_missing( fabrik_t *fab )
{
	// now check for all products (should be changed later for the root)
	for( int i=0;  i<fab->gib_besch()->gib_lieferanten();  i++  ) {
		const ware_besch_t *ware = fab->gib_besch()->gib_lieferant(i)->gib_ware();

		// find out how much is there
		const vector_tpl<ware_production_t>& eingang = fab->gib_eingang();
		uint ware_nr;
		for(  ware_nr=0;  ware_nr<eingang.get_count()  &&  eingang[ware_nr].gib_typ()!=ware;  ware_nr++  ) ;
		if(  eingang[ware_nr].menge > eingang[ware_nr].max/10  ) {
			// already enough supplied to
			continue;
		}

		const vector_tpl <koord> & sources = fab->get_suppliers();
		for( unsigned q=0;  q<sources.get_count();  q++  ) {
			fabrik_t *qfab = fabrik_t::gib_fab(welt,sources[q]);
			const fabrik_besch_t* const fb = qfab->gib_besch();
			for (uint qq = 0; qq < fb->gib_produkte(); qq++) {
				if (fb->gib_produkt(qq)->gib_ware() == ware
					  &&  !is_forbidden( fabrik_t::gib_fab(welt,sources[q]), fab, ware )
					  &&  !is_connected( sources[q], fab->gib_pos().gib_2d(), ware )  ) {
					// find out how much is there
					const vector_tpl<ware_production_t>& ausgang = qfab->gib_ausgang();
					uint ware_nr;
					for(ware_nr=0;  ware_nr<ausgang.get_count()  &&  ausgang[ware_nr].gib_typ()!=ware;  ware_nr++  )
						;
					// ok, there is no connection and it is not banned, so we if there is enough for us
					if(  ((ausgang[ware_nr].menge*4)/3) > ausgang[ware_nr].max  ) {
						// bingo: soure
						start = qfab;
						ziel = fab;
						freight = ware;
						return true;
					}
					else {
						// try something else ...
						if(get_factory_tree_lowest_missing( qfab )) {
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}



/* recursive lookup of a tree and how many factories must be at least connected
 * returns -1, if this tree is incomplete
 */
int ai_goods_t::get_factory_tree_missing_count( fabrik_t *fab )
{
	int numbers=0;	// how many missing?

	// ok, this is a source ...
	if(fab->gib_besch()->gib_lieferanten()==0) {
		return 0;
	}

	// now check for all
	for( int i=0;  i<fab->gib_besch()->gib_lieferanten();  i++  ) {
		const ware_besch_t *ware = fab->gib_besch()->gib_lieferant(i)->gib_ware();

		bool complete = false;	// found at least one factory
		const vector_tpl <koord> & sources = fab->get_suppliers();
		for( unsigned q=0;  q<sources.get_count();  q++  ) {
			fabrik_t *qfab = fabrik_t::gib_fab(welt,sources[q]);
			if(!fab) {
				dbg->error( "fabrik_t::gib_fab()","fab %s at %s does not find supplier at %s.", fab->gib_name(), fab->gib_pos().gib_str(), sources[q].gib_str() );
				continue;
			}
			const fabrik_besch_t* const fb = qfab->gib_besch();
			for (uint qq = 0; qq < fb->gib_produkte(); qq++) {
				if (fb->gib_produkt(qq)->gib_ware() == ware && !is_forbidden( fabrik_t::gib_fab(welt,sources[q]), fab, ware)) {
					int n = get_factory_tree_missing_count( qfab );
					if(n>=0) {
						complete = true;
						if(  !is_connected( sources[q], fab->gib_pos().gib_2d(), ware )  ) {
							numbers += 1;
						}
						numbers += n;
					}
				}
			}
		}
		if(!complete) {
			if(fab->gib_besch()->gib_lieferanten()==0  ||  numbers==0) {
				return -1;
			}
		}
	}
	return numbers;
}



bool ai_goods_t::suche_platz1_platz2(fabrik_t *qfab, fabrik_t *zfab, int length )
{
	clean_marker(platz1,size1);
	clean_marker(platz2,size2);

	koord start( qfab->gib_pos().gib_2d() );
	koord start_size( length, 0 );
	koord ziel( zfab->gib_pos().gib_2d() );
	koord ziel_size( length, 0 );

	bool ok = false;

	if(qfab->gib_besch()->gib_platzierung()!=fabrik_besch_t::Wasser) {
		ok = suche_platz(start, start_size, ziel, qfab->gib_besch()->gib_haus()->gib_groesse() );
	}
	else {
		// water factory => find harbour location
		ok = find_harbour(start, start_size, ziel );
	}
	if(ok) {
		// found a place, search for target
		ok = suche_platz(ziel, ziel_size, start, zfab->gib_besch()->gib_haus()->gib_groesse() );
	}

	INT_CHECK("simplay 1729");

	if( !ok ) {
		// keine Bauplaetze gefunden
		DBG_MESSAGE( "ai_t::suche_platz1_platz2()", "no suitable locations found" );
	}
	else {
		// save places
		platz1 = start;
		size1 = start_size;
		platz2 = ziel;
		size2 = ziel_size;

		DBG_MESSAGE( "ai_t::suche_platz1_platz2()", "platz1=%d,%d platz2=%d,%d", platz1.x, platz1.y, platz2.x, platz2.y );

		// reserve space with marker
		set_marker( platz1, size1 );
		set_marker( platz2, size2 );
	}
	return ok;
}



/* builts dock and ships
 * @author prissi
 */
bool ai_goods_t::create_ship_transport_vehikel(fabrik_t *qfab, int anz_vehikel)
{
	// pak64 has barges ...
	const vehikel_besch_t *v_second = NULL;
	if(ship_vehicle->gib_leistung()==0) {
		v_second = ship_vehicle;
		if(v_second->gib_vorgaenger_count()==0  ||  v_second->gib_vorgaenger(0)==NULL) {
			// pushed barge?
			if(ship_vehicle->gib_nachfolger_count()>0  &&  ship_vehicle->gib_nachfolger(0)!=NULL) {
				v_second = ship_vehicle->gib_nachfolger(0);
			}
			return false;
		}
		else {
			ship_vehicle = v_second->gib_vorgaenger(0);
		}
	}
	DBG_MESSAGE( "ai_goods_t::create_ship_transport_vehikel()", "for %i ships", anz_vehikel );

	// must remove marker
	grund_t* gr = welt->lookup_kartenboden(platz1);
	if (gr) gr->obj_loesche_alle(this);
	// try to built dock
	const haus_besch_t* h = hausbauer_t::gib_random_station(haus_besch_t::hafen, water_wt, welt->get_timeline_year_month(), haltestelle_t::WARE);
	if(h==NULL  ||  !call_general_tool(WKZ_STATION, platz1, h->gib_name())) {
		return false;
	}

	// sea pos (and not on harbour ... )
	halthandle_t halt = haltestelle_t::gib_halt(welt,platz1);
	koord pos1 = platz1 - koord(gr->gib_grund_hang())*h->gib_groesse().y;
	koord best_pos = pos1;
	for(  int y = pos1.y-welt->gib_einstellungen()->gib_station_coverage();  y<=pos1.y+welt->gib_einstellungen()->gib_station_coverage();  y++  ) {
		for(  int x = pos1.x-welt->gib_einstellungen()->gib_station_coverage();  x<=pos1.x+welt->gib_einstellungen()->gib_station_coverage();  x++  ) {
			koord p(x,y);
			const planquadrat_t *plan = welt->lookup(p);
			if(plan) {
				grund_t *gr = plan->gib_kartenboden();
				if(  gr->ist_wasser()  &&  !gr->gib_halt().is_bound()  ) {
					if(plan->get_haltlist_count()>=1  &&  plan->get_haltlist()[0]==halt  &&  abs_distance(best_pos,platz2)<abs_distance(p,platz2)) {
						best_pos = p;
					}
				}
			}
		}
	}

	// no stop position found
	if(best_pos==koord::invalid) {
		return false;
	}

	// since 86.01 we use lines for vehicles ...
	fahrplan_t *fpl=new schifffahrplan_t();
	fpl->append( welt->lookup_kartenboden(best_pos), 0 );
	fpl->append( welt->lookup(qfab->gib_pos()), 100 );
	fpl->aktuell = 1;
	linehandle_t line=simlinemgmt.create_line(simline_t::shipline,fpl);
	delete fpl;

	// now create all vehicles as convois
	for(int i=0;  i<anz_vehikel;  i++) {
		vehikel_t* v = vehikelbauer_t::baue( qfab->gib_pos(), this, NULL, ship_vehicle);
		convoi_t* cnv = new convoi_t(this);
		// V.Meyer: give the new convoi name from first vehicle
		cnv->setze_name(v->gib_besch()->gib_name());
		cnv->add_vehikel( v );

		// two part consist
		if(v_second!=NULL) {
			v = vehikelbauer_t::baue( qfab->gib_pos(), this, NULL, v_second );
			cnv->add_vehikel( v );
		}

		welt->sync_add( cnv );
		cnv->set_line(line);
		cnv->start();
	}
	clean_marker(platz1,size1);
	clean_marker(platz2,size2);
	platz1 += koord(welt->lookup_kartenboden(platz1)->gib_grund_hang());
	return true;
}



/* changed to use vehicles searched before
 * @author prissi
 */
void ai_goods_t::create_road_transport_vehikel(fabrik_t *qfab, int anz_vehikel)
{
	const haus_besch_t* fh = hausbauer_t::gib_random_station(haus_besch_t::generic_stop, road_wt, welt->get_timeline_year_month(), haltestelle_t::WARE);
	// succeed in frachthof creation
	if(fh  &&  call_general_tool(WKZ_STATION, platz1, fh->gib_name())  &&  call_general_tool(WKZ_STATION, platz2, fh->gib_name())  ) {
		koord3d pos1 = welt->lookup(platz1)->gib_kartenboden()->gib_pos();
		koord3d pos2 = welt->lookup(platz2)->gib_kartenboden()->gib_pos();

		int	start_location=0;
		// sometimes, when factories are very close, we need exakt calculation
		const koord3d& qpos = qfab->gib_pos();
		if ((qpos.x - platz1.x) * (qpos.x - platz1.x) + (qpos.y - platz1.y) * (qpos.y - platz1.y) >
				(qpos.x - platz2.x) * (qpos.x - platz2.x) + (qpos.y - platz2.y) * (qpos.y - platz2.y)) {
			start_location = 1;
		}

		// calculate vehicle start position
		koord3d startpos=(start_location==0)?pos1:pos2;
		ribi_t::ribi w_ribi = welt->lookup(startpos)->gib_weg_ribi_unmasked(road_wt);
		// now start all vehicle one field before, so they load immediately
		startpos = welt->lookup(koord(startpos.gib_2d())+koord(w_ribi))->gib_kartenboden()->gib_pos();

		// since 86.01 we use lines for road vehicles ...
		fahrplan_t *fpl=new autofahrplan_t();
		fpl->append(welt->lookup(pos1), start_location == 0 ? 100 : 0);
		fpl->append(welt->lookup(pos2), start_location == 1 ? 100 : 0);
		fpl->aktuell = start_location;
		linehandle_t line=simlinemgmt.create_line(simline_t::truckline,fpl);
		delete fpl;

		// now create all vehicles as convois
		for(int i=0;  i<anz_vehikel;  i++) {
			vehikel_t* v = vehikelbauer_t::baue(startpos, this, NULL, road_vehicle);
			convoi_t* cnv = new convoi_t(this);
			// V.Meyer: give the new convoi name from first vehicle
			cnv->setze_name(v->gib_besch()->gib_name());
			cnv->add_vehikel( v );

			welt->sync_add( cnv );
			cnv->set_line(line);
			cnv->start();
		}
	}
}



/* now obeys timeline and use "more clever" scheme for vehicle selection *
 * @author prissi
 */
void ai_goods_t::create_rail_transport_vehikel(const koord platz1, const koord platz2, int anz_vehikel, int ladegrad)
{
	fahrplan_t *fpl;
	convoi_t* cnv = new convoi_t(this);
	koord3d pos1= welt->lookup(platz1)->gib_kartenboden()->gib_pos();
	koord3d pos2 = welt->lookup(platz2)->gib_kartenboden()->gib_pos();

	// probably need to electrify the track?
	if(  rail_engine->get_engine_type()==vehikel_besch_t::electric  ) {
		// we need overhead wires
		const way_obj_besch_t *e = wayobj_t::wayobj_search(track_wt,overheadlines_wt,welt->get_timeline_year_month());
		wkz_wayobj_t wkz;
		wkz.default_param = e->gib_name();
		wkz.init( welt, this );
		wkz.work( welt, this, welt->lookup_kartenboden(platz1)->gib_pos() );
		wkz.work( welt, this, welt->lookup_kartenboden(platz2)->gib_pos() );
		wkz.exit( welt, this );
	}
	vehikel_t* v = vehikelbauer_t::baue(pos2, this, NULL, rail_engine);

	// V.Meyer: give the new convoi name from first vehicle
	cnv->setze_name(rail_engine->gib_name());
	cnv->add_vehikel( v );

	DBG_MESSAGE( "ai_goods_t::create_rail_transport_vehikel","for %i cars",anz_vehikel);

	/* now we add cars:
	 * check here also for introduction years
	 */
	for(int i = 0; i < anz_vehikel; i++) {
		// use the vehicle we searched before
		vehikel_t* v = vehikelbauer_t::baue(pos2, this, NULL, rail_vehicle);
		cnv->add_vehikel( v );
	}

	fpl = cnv->gib_vehikel(0)->erzeuge_neuen_fahrplan();

	fpl->aktuell = 0;
	fpl->append(welt->lookup(pos1), ladegrad);
	fpl->append(welt->lookup(pos2), 0);

	cnv->setze_fahrplan(fpl);
	welt->sync_add( cnv );
	cnv->start();
}



/* built a station
 * Can fail even though check has been done before
 * @author prissi
 */
int ai_goods_t::baue_bahnhof(const koord* p, int anz_vehikel)
{
	int laenge = max(((rail_vehicle->get_length()*anz_vehikel)+rail_engine->get_length()+TILE_STEPS-1)/TILE_STEPS,1);

	int baulaenge = 0;
	ribi_t::ribi ribi = welt->lookup_kartenboden(*p)->gib_weg_ribi(track_wt);
	koord zv ( ribi );
	koord t = *p;
	bool ok = true;

	for(  int i=0;  i<laenge;  i++  ) {
		grund_t *gr = welt->lookup_kartenboden(t);
		ok &= (gr != NULL) &&  !gr->has_two_ways()  &&  gr->gib_weg_hang()==hang_t::flach;
		if(!ok) {
			break;
		}
		baulaenge ++;
		t += zv;
	}

	// too short
	if(baulaenge<=1) {
		return 0;
	}

	// to avoid broken stations, they will be always built next to an existing
	bool make_all_bahnhof=false;

	// find a freight train station
	const haus_besch_t* besch = hausbauer_t::gib_random_station(haus_besch_t::generic_stop, track_wt, welt->get_timeline_year_month(), haltestelle_t::WARE);
	if(besch==NULL) {
		// no freight station
		return 0;
	}
	koord pos;
	for(  pos=t-zv;  pos!=*p;  pos-=zv ) {
		if(  make_all_bahnhof  ||  is_my_halt(pos+koord(-1,-1))  ||  is_my_halt(pos+koord(-1,1))  ||  is_my_halt(pos+koord(1,-1))  ||  is_my_halt(pos+koord(1,1))  ) {
			// start building, if next to an existing station
			make_all_bahnhof = true;
			call_general_tool( WKZ_STATION, pos, besch->gib_name() );
		}
		INT_CHECK("simplay 753");
	}
	// now add the other squares (going backwards)
	for(  pos=*p;  pos!=t;  pos+=zv ) {
		if(  !is_my_halt(pos)  ) {
			call_general_tool( WKZ_STATION, pos, besch->gib_name() );
		}
	}

	laenge = min( anz_vehikel, (baulaenge*TILE_STEPS - rail_engine->get_length())/rail_vehicle->get_length() );
//DBG_MESSAGE("ai_goods_t::baue_bahnhof","Final station at (%i,%i) with %i tiles for %i cars",p->x,p->y,baulaenge,laenge);
	return laenge;
}



/* built a very simple track with just the minimum effort
 * usually good enough, since it can use road crossings
 * @author prissi
 */
bool ai_goods_t::create_simple_rail_transport()
{
	clean_marker(platz1,size1);
	clean_marker(platz2,size2);

	bool ok=true;
	// first: make plain stations tiles as intended
	sint16 z1 = max( welt->gib_grundwasser()+Z_TILE_STEP, welt->lookup_kartenboden(platz1)->gib_hoehe() );
	koord k = platz1;
	koord diff1( sgn(size1.x), sgn(size1.y) );
	koord perpend( sgn(size1.y), sgn(size1.x) );
	ribi_t::ribi ribi1 = ribi_typ( diff1 );
	while(k!=size1+platz1) {
		if(!welt->ebne_planquadrat( this, k, z1 )) {
			ok = false;
			break;
		}
		grund_t *gr = welt->lookup_kartenboden(k);
		weg_t *sch = weg_t::alloc(track_wt);
		sch->setze_besch( rail_weg );
		int cost = -gr->neuen_weg_bauen(sch, ribi1, this) - rail_weg->gib_preis();
		buche(cost, k, COST_CONSTRUCTION);
		ribi1 = ribi_t::doppelt( ribi1 );
		k += diff1;
	}

	// make the second ones flat ...
	sint16 z2 = max( welt->gib_grundwasser()+Z_TILE_STEP, welt->lookup_kartenboden(platz2)->gib_hoehe() );
	k = platz2;
	perpend = koord( sgn(size2.y), sgn(size2.x) );
	koord diff2( sgn(size2.x), sgn(size2.y) );
	ribi_t::ribi ribi2 = ribi_typ( diff2 );
	while(k!=size2+platz2  &&  ok) {
		if(!welt->ebne_planquadrat(this,k,z2)) {
			ok = false;
			break;
		}
		grund_t *gr = welt->lookup_kartenboden(k);
		weg_t *sch = weg_t::alloc(track_wt);
		sch->setze_besch( rail_weg );
		int cost = -gr->neuen_weg_bauen(sch, ribi2, this) - rail_weg->gib_preis();
		buche(cost, k, COST_CONSTRUCTION);
		ribi2 = ribi_t::doppelt( ribi2 );
		k += diff2;
	}

	// now calc the route
	wegbauer_t bauigel(welt, this);
	if(ok) {
		bauigel.route_fuer( (wegbauer_t::bautyp_t)(wegbauer_t::schiene|wegbauer_t::bot_flag), rail_weg, tunnelbauer_t::find_tunnel(track_wt,rail_engine->gib_geschw(),welt->get_timeline_year_month()), brueckenbauer_t::find_bridge(track_wt,rail_engine->gib_geschw(),welt->get_timeline_year_month()) );
		bauigel.set_keep_existing_ways(false);
		bauigel.calc_route( koord3d(platz2+size2,z2), koord3d(platz1+size1,z1) );
		INT_CHECK("simplay 2478");
	}

	if(ok  &&  bauigel.max_n > 3) {
#if 0
/* FIX THIS! */
		//just check, if I could not start at the other end of the station ...
		int start=0, end=bauigel.max_n;
		for( int j=1;  j<bauigel.max_n-1;  j++  ) {
			if(bauigel.gib_route_bei(j)==platz2-diff2) {
				start = j;
				platz2 = platz2+size2-diff2;
				size2 = size2*(-1);
				diff2 = diff2*(-1);
			}
			if(bauigel.gib_route_bei(j)==platz1-diff1) {
				end = j;
				platz1 = platz1+size1-diff1;
				size1 = size1*(-1);
				diff1 = diff1*(-1);
			}
		}
		// so found shorter route?
		if(start!=0  ||  end!=bauigel.max_n) {
			bauigel.calc_route( koord3d(platz2+size2,z2), koord3d(platz1+size1,z1) );
		}
#endif

DBG_MESSAGE("ai_goods_t::create_simple_rail_transport()","building simple track from %d,%d to %d,%d",platz1.x, platz1.y, platz2.x, platz2.y);
		bauigel.baue();
		// connect to track
		ribi1 = ribi_typ(diff1);
		assert( welt->lookup_kartenboden(platz1+size1-diff1)->weg_erweitern(track_wt, ribi1) );
		ribi1 = ribi_t::rueckwaerts(ribi1);
		assert( welt->lookup_kartenboden(platz1+size1)->weg_erweitern(track_wt, ribi1) );
		ribi2 = ribi_typ(diff2);
		assert( welt->lookup_kartenboden(platz2+size2-diff2)->weg_erweitern(track_wt, ribi2) );
		ribi2 = ribi_t::rueckwaerts(ribi2);
		assert( welt->lookup_kartenboden(platz2+size2)->weg_erweitern(track_wt, ribi2) );
		return true;
	}
	else {
		// not ok: remove station ...
		k=platz1;
		while(k!=size1+platz1) {
			int cost = -welt->lookup_kartenboden(k)->weg_entfernen( track_wt, true );
			buche(cost, k, COST_CONSTRUCTION);
			k += diff1;
		}
		k=platz2;
		while(k!=size2+platz2) {
			int cost = -welt->lookup_kartenboden(k)->weg_entfernen( track_wt, true );
			buche(cost, k, COST_CONSTRUCTION);
			k += diff2;
		}
	}
	return false;
}



// the normal length procedure for freigth AI
void ai_goods_t::step()
{
	// needed for schedule of stops ...
	spieler_t::step();

	if(!automat) {
		// I am off ...
		return;
	}

	// one route per month ...
	if(  welt->gib_steps() < next_contruction_steps  ) {
		return;
	}

	if(konto_ueberzogen>0) {
		// nothing to do but to remove unneeded convois to gain some money
		state = CHECK_CONVOI;
	}

	switch(state) {

		case NR_INIT:
			state = NR_SAMMLE_ROUTEN;
			count = 0;
			built_update_headquarter();
			if(root==NULL) {
				// find a tree root to complete
				weighted_vector_tpl<fabrik_t *> start_fabs(20);
				slist_iterator_tpl<fabrik_t *> fabiter( welt->gib_fab_list() );
				while(fabiter.next()) {
					fabrik_t *fab = fabiter.get_current();
					// consumer and not completely overcrowded
					if(fab->gib_besch()->gib_produkte()==0  &&  fab->get_status()!=fabrik_t::bad) {
						int missing = get_factory_tree_missing_count( fab );
						if(missing>0) {
							start_fabs.append( fab, 100/(missing+1)+1, 20 );
						}
					}
				}
				if(start_fabs.get_count()>0) {
					root = start_fabs.at_weight( simrand( start_fabs.get_sum_weight() ) );
				}
			}
			// still nothing => we have to check convois ...
			if(root==NULL) {
				state = CHECK_CONVOI;
			}
		break;

		/* try to built a network:
		* last target also new target ...
		*/
		case NR_SAMMLE_ROUTEN:
			if(get_factory_tree_lowest_missing(root)) {
				if(  start->gib_besch()->gib_platzierung()!=fabrik_besch_t::Wasser  ||  vehikelbauer_t::vehikel_search( water_wt, welt->get_timeline_year_month(), 0, 10, freight, false, false )!=NULL  ) {
					DBG_MESSAGE("ai_goods_t::do_ki", "Consider route from %s (%i,%i) to %s (%i,%i)", start->gib_name(), start->gib_pos().x, start->gib_pos().y, ziel->gib_name(), ziel->gib_pos().x, ziel->gib_pos().y );
					state = NR_BAUE_ROUTE1;
				}
				else {
					// add to impossible connections
					forbidden_conections.append( fabconnection_t( start, ziel, freight ) );
					state = CHECK_CONVOI;
				}
			}
			else {
				// did all I could do here ...
				root = NULL;
				state = CHECK_CONVOI;
			}
		break;

		// now we need so select the cheapest mean to get maximum profit
		case NR_BAUE_ROUTE1:
		{
			/* if we reached here, we decide to built a route;
			 * the KI just chooses the way to run the operation at maximum profit (minimum loss).
			 * The KI will built also a loosing route; this might be required by future versions to
			 * be able to built a network!
			 * @author prissi
			 */

			/* for the calculation we need:
			 * a suitable car (and engine)
			 * a suitable weg
			 */
			koord zv = start->gib_pos().gib_2d() - ziel->gib_pos().gib_2d();
			int dist = abs(zv.x) + abs(zv.y);

			// guess the "optimum" speed (usually a little too low)
			uint32 best_rail_speed = 80;// is ok enough for goods, was: min(60+freight->gib_speed_bonus()*5, 140 );
			uint32 best_road_speed = min(60+freight->gib_speed_bonus()*5, 130 );

			// obey timeline
			uint month_now = (welt->use_timeline() ? welt->get_current_month() : 0);

			INT_CHECK("simplay 1265");

			// is rail transport allowed?
			if(rail_transport) {
				// any rail car that transport this good (actually this weg_t the largest)
				rail_vehicle = vehikelbauer_t::vehikel_search( track_wt, month_now, 0, best_rail_speed,  freight, true, false );
			}
			rail_engine = NULL;
			rail_weg = NULL;
DBG_MESSAGE("do_ki()","rail vehicle %p",rail_vehicle);

			// is road transport allowed?
			if(road_transport) {
				// any road car that transport this good (actually this returns the largest)
				road_vehicle = vehikelbauer_t::vehikel_search( road_wt, month_now, 10, best_road_speed, freight, false, false );
			}
			road_weg = NULL;
DBG_MESSAGE("do_ki()","road vehicle %p",road_vehicle);

			ship_vehicle = NULL;
			if(start->gib_besch()->gib_platzierung()==fabrik_besch_t::Wasser) {
				// largest ship available
				ship_vehicle = vehikelbauer_t::vehikel_search( water_wt, month_now, 0, 20, freight, false, false );
			}

			INT_CHECK("simplay 1265");


			// properly calculate production
			const vector_tpl<ware_production_t>& ausgang = start->gib_ausgang();
			uint start_ware=0;
			while(  start_ware<ausgang.get_count()  &&  ausgang[start_ware].gib_typ()!=freight  ) {
				start_ware++;
			}
			assert(  start_ware<ausgang.get_count()  );
			const int prod = min(ziel->get_base_production(),
			                 ( start->get_base_production() * start->gib_besch()->gib_produkt(start_ware)->gib_faktor() )/256u - start->gib_abgabe_letzt(start_ware) );

DBG_MESSAGE("do_ki()","check railway");
			/* calculate number of cars for railroad */
			count_rail=255;	// no cars yet
			if(  rail_vehicle!=NULL  ) {
				// if our car is faster: well use slower speed to save money
			 	best_rail_speed = min(51,rail_vehicle->gib_geschw());
				// for engine: gues number of cars
				count_rail = (prod*dist) / (rail_vehicle->gib_zuladung()*best_rail_speed)+1;
				// assume the engine weight 100 tons for power needed calcualtion
				int total_weight = count_rail*( (rail_vehicle->gib_zuladung()*freight->gib_weight_per_unit())/1000 + rail_vehicle->gib_gewicht());
//				long power_needed = (long)(((best_rail_speed*best_rail_speed)/2500.0+1.0)*(100.0+count_rail*(rail_vehicle->gib_gewicht()+rail_vehicle->gib_zuladung()*freight->gib_weight_per_unit()*0.001)));
				rail_engine = vehikelbauer_t::vehikel_search( track_wt, month_now, total_weight, best_rail_speed, NULL, wayobj_t::default_oberleitung!=NULL, false );
				if(  rail_engine!=NULL  ) {
					best_rail_speed = min(rail_engine->gib_geschw(),rail_vehicle->gib_geschw());
					// find cheapest track with that speed (and no monorail/elevated/tram tracks, please)
					rail_weg = wegbauer_t::weg_search( track_wt, best_rail_speed, welt->get_timeline_year_month(),weg_t::type_flat );
					if(  rail_weg!=NULL  ) {
						if(  best_rail_speed>rail_weg->gib_topspeed()  ) {
							best_rail_speed = rail_weg->gib_topspeed();
						}
						// no train can have more than 15 cars
						count_rail = min( 22, (3*prod*dist) / (rail_vehicle->gib_zuladung()*best_rail_speed*2) );
						// if engine too week, reduce number of cars
						if(  count_rail*80*64>(int)(rail_engine->gib_leistung()*rail_engine->get_gear())  ) {
							count_rail = rail_engine->gib_leistung()*rail_engine->get_gear()/(80*64);
						}
						count_rail = ((count_rail+1)&0x0FE)+1;
DBG_MESSAGE("ai_goods_t::do_ki()","Engine %s guess to need %d rail cars %s for route (%s)", rail_engine->gib_name(), count_rail, rail_vehicle->gib_name(), rail_weg->gib_name() );
					}
				}
				if(  rail_engine==NULL  ||  rail_weg==NULL  ) {
					// no rail transport possible
DBG_MESSAGE("ai_goods_t::do_ki()","No railway possible.");
					rail_vehicle = NULL;
					count_rail = 255;
				}
			}

			INT_CHECK("simplay 1265");

DBG_MESSAGE("do_ki()","check railway");
			/* calculate number of cars for road; much easier */
			count_road=255;	// no cars yet
			if(  road_vehicle!=NULL  ) {
				best_road_speed = road_vehicle->gib_geschw();
				// find cheapest road
				road_weg = wegbauer_t::weg_search( road_wt, best_road_speed, welt->get_timeline_year_month(),weg_t::type_flat );
				if(  road_weg!=NULL  ) {
					if(  best_road_speed>road_weg->gib_topspeed()  ) {
						best_road_speed = road_weg->gib_topspeed();
					}
					// minimum vehicle is 1, maximum vehicle is 48, more just result in congestion
					count_road = min( 254, (prod*dist) / (road_vehicle->gib_zuladung()*best_road_speed*2)+2 );
DBG_MESSAGE("ai_goods_t::do_ki()","guess to need %d road cars %s for route %s", count_road, road_vehicle->gib_name(), road_weg->gib_name() );
				}
				else {
					// no roads there !?!
DBG_MESSAGE("ai_goods_t::do_ki()","No roadway possible.");
				}
			}

			// find the cheapest transport ...
			// assume maximum cost
			int	cost_rail=0x7FFFFFFF, cost_road=0x7FFFFFFF;
			int	income_rail=0, income_road=0;

			// calculate cost for rail
			if(  count_rail<255  ) {
				int freight_price = (freight->gib_preis()*rail_vehicle->gib_zuladung()*count_rail)/24*((8000+(best_rail_speed-80)*freight->gib_speed_bonus())/1000);
				// calculated here, since the above number was based on production
				// only uneven number of cars bigger than 3 makes sense ...
				count_rail = max( 3, count_rail );
				income_rail = (freight_price*best_rail_speed)/(2*dist+count_rail);
				cost_rail = rail_weg->gib_wartung() + (((count_rail+1)/2)*300)/dist + ((count_rail*rail_vehicle->gib_betriebskosten()+rail_engine->gib_betriebskosten())*best_rail_speed)/(2*dist+count_rail);
				DBG_MESSAGE("ai_goods_t::do_ki()","Netto credits per day for rail transport %.2f (income %.2f)",cost_rail/100.0, income_rail/100.0 );
				cost_rail -= income_rail;
			}

			// and calculate cost for road
			if(  count_road<255  ) {
				// for short distance: reduce number of cars
				// calculated here, since the above number was based on production
				count_road = CLIP( (dist*15)/best_road_speed, 2, count_road );
				int freight_price = (freight->gib_preis()*road_vehicle->gib_zuladung()*count_road)/24*((8000+(best_road_speed-80)*freight->gib_speed_bonus())/1000);
				cost_road = road_weg->gib_wartung() + 300/dist + (count_road*road_vehicle->gib_betriebskosten()*best_road_speed)/(2*dist+5);
				income_road = (freight_price*best_road_speed)/(2*dist+5);
				DBG_MESSAGE("ai_goods_t::do_ki()","Netto credits per day and km for road transport %.2f (income %.2f)",cost_road/100.0, income_road/100.0 );
				cost_road -= income_road;
			}

			// check location, if vehicles found
			if(  min(count_road,count_rail)!=255  ) {
				// road or rail?
				int length = 1;
				if(  cost_rail<cost_road  ) {
					length = (rail_engine->get_length() + count_rail*rail_vehicle->get_length()+TILE_STEPS-1)/TILE_STEPS;
					if(suche_platz1_platz2(start, ziel, length)) {
						state = ship_vehicle ? NR_BAUE_WATER_ROUTE : NR_BAUE_SIMPLE_SCHIENEN_ROUTE;
						next_contruction_steps += 80;
					}
				}
				// if state is still NR_BAUE_ROUTE1 then there are no sutiable places
				if(state==NR_BAUE_ROUTE1  &&  suche_platz1_platz2(start, ziel, 1)) {
					// rail was too expensive or not successfull
					count_rail = 255;
					state = ship_vehicle ? NR_BAUE_WATER_ROUTE : NR_BAUE_STRASSEN_ROUTE;
					next_contruction_steps = 80;
				}
			}
			// no success at all?
			if(state==NR_BAUE_ROUTE1) {
				// maybe this route is not builtable ... add to forbidden connections
				forbidden_conections.append( fabconnection_t( start, ziel, freight ) );
				ziel = NULL;	// otherwise it may always try to built the same route!
				state = CHECK_CONVOI;
			}
		}
		break;

		// built a simple ship route
		case NR_BAUE_WATER_ROUTE:
			if(is_connected(start->gib_pos().gib_2d(), ziel->gib_pos().gib_2d(), freight)) {
				state = CHECK_CONVOI;
			}
			else {
				// properly calculate production
				const vector_tpl<ware_production_t>& ausgang = start->gib_ausgang();
				uint start_ware=0;
				while(  start_ware<ausgang.get_count()  &&  ausgang[start_ware].gib_typ()!=freight  ) {
					start_ware++;
				}
				koord harbour=platz1;
				const int prod = min( ziel->get_base_production(), (start->get_base_production() * start->gib_besch()->gib_produkt(start_ware)->gib_faktor()) - start->gib_abgabe_letzt(start_ware) );
				if(prod<0) {
					// too much supplied last time?!? => retry
					state = CHECK_CONVOI;
					break;
				}
				int ships_needed = 1 + (prod*abs_distance(harbour,start->gib_pos().gib_2d())) / (ship_vehicle->gib_zuladung()*max(20,ship_vehicle->gib_geschw()));
				if(create_ship_transport_vehikel(start,ships_needed)) {
					if(welt->lookup(harbour)->gib_halt()->gib_fab_list().contains(ziel)) {
						// so close, so we are already connected
						grund_t *gr = welt->lookup_kartenboden(platz2);
						if (gr) gr->obj_loesche_alle(this);
						state = NR_ROAD_SUCCESS;
					}
					else {
						// else we need to built the second part of the route
						state = (rail_vehicle  &&  count_rail<255) ? NR_BAUE_SIMPLE_SCHIENEN_ROUTE : NR_BAUE_STRASSEN_ROUTE;
					}
				}
				else {
					ship_vehicle = NULL;
					state = CHECK_CONVOI;
				}
			}
			break;

		// built a simple railroad
		case NR_BAUE_SIMPLE_SCHIENEN_ROUTE:
			if(is_connected(start->gib_pos().gib_2d(), ziel->gib_pos().gib_2d(), freight)) {
				state = ship_vehicle ? NR_BAUE_CLEAN_UP : CHECK_CONVOI;
			}
			else if(create_simple_rail_transport()) {
				sint16 org_count_rail = count_rail;
				count_rail = baue_bahnhof(&platz1, count_rail);
				if(count_rail>=3) {
					count_rail = baue_bahnhof(&platz2, count_rail);
				}
				if(count_rail>=3) {
					if(count_rail<org_count_rail) {
						// rethink engine
					 	int best_rail_speed = min(51,rail_vehicle->gib_geschw());
					  	// obey timeline
						uint month_now = (welt->use_timeline() ? welt->get_current_month() : 0);
						// for engine: gues number of cars
						long power_needed=(long)(((best_rail_speed*best_rail_speed)/2500.0+1.0)*(100.0+count_rail*(rail_vehicle->gib_gewicht()+rail_vehicle->gib_zuladung()*freight->gib_weight_per_unit()*0.001)));
						const vehikel_besch_t *v=vehikelbauer_t::vehikel_search( track_wt, month_now, power_needed, best_rail_speed, NULL, false, false );
						if(v->gib_betriebskosten()<rail_engine->gib_betriebskosten()) {
							rail_engine = v;
						}
					}
					create_rail_transport_vehikel( platz1, platz2, count_rail, 100 );
					state = NR_RAIL_SUCCESS;
				}
				else {
DBG_MESSAGE("ai_goods_t::step()","remove already constructed rail between %i,%i and %i,%i and try road",platz1.x,platz1.y,platz2.x,platz2.y);
					// no sucess: clean route
					char param[16];
					sprintf( param, "%i", track_wt );
					wkz_wayremover_t wkz;
					wkz.default_param = param;
					wkz.init( welt, this );
					wkz.work( welt, this, welt->lookup_kartenboden(platz1)->gib_pos() );
					wkz.work( welt, this, welt->lookup_kartenboden(platz2)->gib_pos() );
					wkz.exit( welt, this );
				}
			}
			else {
				state = NR_BAUE_STRASSEN_ROUTE;
			}
		break;

		// built a simple road (no bridges, no tunnels)
		case NR_BAUE_STRASSEN_ROUTE:
			if(is_connected(start->gib_pos().gib_2d(), ziel->gib_pos().gib_2d(), freight)) {
				state = ship_vehicle ? NR_BAUE_CLEAN_UP : CHECK_CONVOI;
			}
			else if(create_simple_road_transport(platz1,size1,platz2,size2,road_weg)) {
				create_road_transport_vehikel(start, count_road );
				state = NR_ROAD_SUCCESS;
			}
			else {
				state = NR_BAUE_CLEAN_UP;
			}
		break;

		// remove marker etc.
		case NR_BAUE_CLEAN_UP:
		{
			forbidden_conections.append( fabconnection_t( start, ziel, freight ) );
			if(ship_vehicle) {
				// only here, if we could built ships but no connection
				halthandle_t start_halt;
				for( int r=0;  r<4;  r++  ) {
					start_halt = haltestelle_t::gib_halt(welt,platz1+koord::nsow[r]);
					if(start_halt.is_bound()  &&  (start_halt->get_station_type()&haltestelle_t::dock)!=0) {
						// delete all ships on this line
						vector_tpl<linehandle_t> lines;
						simlinemgmt.get_lines( simline_t::shipline, &lines );
						if(!lines.empty()) {
							linehandle_t line = lines.back();
							fahrplan_t *fpl=line->get_fahrplan();
							if(fpl->maxi()>1  &&  haltestelle_t::gib_halt(welt,fpl->eintrag[0].pos)==start_halt) {
								while(line->count_convoys()>0) {
									line->get_convoy(0)->self_destruct();
									line->get_convoy(0)->step();
								}
							}
							simlinemgmt.delete_line( line );
						}
						// delete harbour
						call_general_tool( WKZ_REMOVER, platz1+koord::nsow[r], NULL );
						break;
					}
				}
			}
			// otherwise it may always try to built the same route!
			ziel = NULL;
			// schilder aufraeumen
			clean_marker(platz1,size1);
			clean_marker(platz2,size2);
			state = CHECK_CONVOI;
			break;
		}

		// successful rail construction
		case NR_RAIL_SUCCESS:
		{
			// tell the player
			char buf[256];
			const koord3d& spos = start->gib_pos();
			const koord3d& zpos = ziel->gib_pos();
			sprintf(buf, translator::translate("%s\nopened a new railway\nbetween %s\nat (%i,%i) and\n%s at (%i,%i)."), gib_name(), translator::translate(start->gib_name()), spos.x, spos.y, translator::translate(ziel->gib_name()), zpos.x, zpos.y);
			welt->get_message()->add_message(buf, spos.gib_2d(), message_t::ai,player_nr,rail_engine->gib_basis_bild());

			state = CHECK_CONVOI;
		}
		break;

		// successful rail construction
		case NR_ROAD_SUCCESS:
		{
			// tell the player
			char buf[256];
			const koord3d& spos = start->gib_pos();
			const koord3d& zpos = ziel->gib_pos();
			sprintf(buf, translator::translate("%s\nnow operates\n%i trucks between\n%s at (%i,%i)\nand %s at (%i,%i)."), gib_name(), count_road, translator::translate(start->gib_name()), spos.x, spos.y, translator::translate(ziel->gib_name()), zpos.x, zpos.y);
			welt->get_message()->add_message(buf, spos.gib_2d(), message_t::ai, player_nr, road_vehicle->gib_basis_bild());
			state = CHECK_CONVOI;
		}
		break;

		// remove stucked vehicles (only from roads!)
		case CHECK_CONVOI:
		{
			next_contruction_steps = welt->gib_steps() + simrand( 8000 )+1000;

			for( int i = welt->get_convoi_count()-1;  i>=0;  i--  ) {
				const convoihandle_t cnv = welt->get_convoi(i);
				if(cnv->gib_besitzer()!=this) {
					continue;
				}

				if(cnv->gib_vehikel(0)->gib_waytype()==water_wt) {
					// ships will be only deleted together with the connecting convoi
					continue;
				}

				long gewinn = 0;
				for( int j=0;  j<12;  j++  ) {
					gewinn += cnv->get_finance_history( j, CONVOI_PROFIT );
				}

				// apparently we got the toatlly wrong vehicle here ...
				// (but we will delete it only, if we need, because it may be needed for a chain)
				bool delete_this = (konto_ueberzogen>0)  &&  (gewinn < -(sint32)cnv->calc_restwert());

				// check for empty vehicles (likely stucked) that are making no plus and remove them ...
				// take care, that the vehicle is old enough ...
				if(!delete_this  &&  (welt->get_current_month()-cnv->gib_vehikel(0)->gib_insta_zeit())>6  &&  gewinn<=0  ){
					sint64 goods=0;
					// no goods for six months?
					for( int i=0;  i<6;  i ++) {
						goods += cnv->get_finance_history(i,CONVOI_TRANSPORTED_GOODS);
					}
					delete_this = (goods==0);
				}

				// well, then delete this (likely stucked somewhere) or insanely unneeded
				if(delete_this) {
					waytype_t wt = cnv->gib_vehikel(0)->gib_besch()->get_waytype();
					linehandle_t line = cnv->get_line();
					DBG_MESSAGE("ai_goods_t::do_ki()","%s retires convoi %s!", gib_name(), cnv->gib_name());

					koord3d start_pos, end_pos;
					fahrplan_t *fpl = cnv->gib_fahrplan();
					if(fpl  &&  fpl->maxi()>1) {
						start_pos = fpl->eintrag[0].pos;
						end_pos = fpl->eintrag[1].pos;
					}

					cnv->self_destruct();
					cnv->step();	// to really get rid of it

					// last vehicle on that connection (no line => railroad)
					if(  !line.is_bound()  ||  line->count_convoys()==0  ) {
						// check if a conncetion boat must be removed
						halthandle_t start_halt = haltestelle_t::gib_halt(welt,start_pos);
						if(start_halt.is_bound()  &&  (start_halt->get_station_type()&haltestelle_t::dock)!=0) {
							// delete all ships on this line
							vector_tpl<linehandle_t> lines;
							koord water_stop = koord::invalid;
							simlinemgmt.get_lines( simline_t::shipline, &lines );
							for (vector_tpl<linehandle_t>::const_iterator iter2 = lines.begin(), end = lines.end(); iter2 != end; iter2++) {
								linehandle_t line = *iter2;
								fahrplan_t *fpl=line->get_fahrplan();
								if(fpl->maxi()>1  &&  haltestelle_t::gib_halt(welt,fpl->eintrag[0].pos)==start_halt) {
									water_stop = koord( (start_pos.x+fpl->eintrag[0].pos.x)/2, (start_pos.y+fpl->eintrag[0].pos.y)/2 );
									while(line->count_convoys()>0) {
										line->get_convoy(0)->self_destruct();
										line->get_convoy(0)->step();
									}
								}
								simlinemgmt.delete_line( line );
							}
							// delete harbour
							call_general_tool( WKZ_REMOVER, water_stop, NULL );
						}
					}

					if(wt==track_wt) {
						char param[16];
						sprintf( param, "%i", track_wt );
						wkz_wayremover_t wkz;
						wkz.default_param = param;
						wkz.init( welt, this );
						wkz.work( welt, this, start_pos );
						wkz.work( welt, this, end_pos );
						wkz.exit( welt, this );
					}
					else {
						// last convoi => remove completely<
						if(line.is_bound()  &&  line->count_convoys()==0) {
							simlinemgmt.delete_line( line );

							char param[16];
							sprintf( param, "%i", wt );
							wkz_wayremover_t wkz;
							wkz.default_param = param;
							wkz.init( welt, this );
							wkz.work( welt, this, start_pos );
							if(wkz.work( welt, this, end_pos )!=NULL) {
								// cannot remove all => likely some other convois there too
								// remove loading bays and road on start and end, if we cannot remove the whole way
								wkz.work( welt, this, start_pos );
								wkz.work( welt, this, start_pos );
								wkz.work( welt, this, end_pos );
								wkz.work( welt, this, end_pos );
							}
							wkz.exit( welt, this );
						}
					}
					break;
				}
			}
			state = NR_INIT;
		}
		break;

		default:
			dbg->fatal("ai_goods_t::step()","Illegal state!", state );
			state = NR_INIT;
	}
}



void ai_goods_t::rdwr(loadsave_t *file)
{
	xml_tag_t t( file, "ai_goods_t" );

	// first: do all the administration
	spieler_t::rdwr(file);

	// then check, if we have to do something or the game is too old ...
	if(file->get_version()<101000) {
		// ignore saving, reinit on loading
		if(  file->is_loading()  ) {
			state = NR_INIT;

			road_vehicle = NULL;
			road_weg = NULL;

			next_contruction_steps = welt->gib_steps()+simrand(400);

			road_transport = player_nr!=7;
			rail_transport = player_nr>3;
			ship_transport = true;

			root = start = ziel = NULL;
		}
		return;
	}

	// now save current state ...
	file->rdwr_enum(state, " ");
	platz1.rdwr( file );
	size1.rdwr( file );
	platz2.rdwr( file );
	size2.rdwr( file );
	file->rdwr_long(count_rail, "" );
	file->rdwr_long(count_road, "" );
	file->rdwr_long(count, "" );
	file->rdwr_bool( road_transport, "" );
	file->rdwr_bool( rail_transport, "" );
	file->rdwr_bool( ship_transport, "" );

	if(file->is_saving()) {
		// save current pointers
		sint32 delta_steps = next_contruction_steps-welt->gib_steps();
		file->rdwr_long(delta_steps, " ");
		koord3d k3d = root ? root->gib_pos() : koord3d::invalid;
		k3d.rdwr(file);
		k3d = start ? start->gib_pos() : koord3d::invalid;
		k3d.rdwr(file);
		k3d = ziel ? ziel->gib_pos() : koord3d::invalid;
		k3d.rdwr(file);
		// what freight?
		const char *s = freight ? freight->gib_name() : NULL;
		file->rdwr_str( s );
		// vehicles besch
		s = rail_engine ? rail_engine->gib_name() : NULL;
		file->rdwr_str( s );
		s = rail_vehicle ? rail_vehicle->gib_name() : NULL;
		file->rdwr_str( s );
		s = road_vehicle ? road_vehicle->gib_name() : NULL;
		file->rdwr_str( s );
		s = ship_vehicle ? ship_vehicle->gib_name() : NULL;
		file->rdwr_str( s );
		// ways
		s = rail_weg ? rail_weg->gib_name() : NULL;
		file->rdwr_str( s );
		s = road_weg ? road_weg->gib_name() : NULL;
		file->rdwr_str( s );
	}
	else {
		// since steps in loaded game == 0
		file->rdwr_long(next_contruction_steps, " ");
		// reinit current pointers
		koord3d k3d;
		k3d.rdwr(file);
		root = fabrik_t::gib_fab( welt, k3d.gib_2d() );
		k3d.rdwr(file);
		start = fabrik_t::gib_fab( welt, k3d.gib_2d() );
		k3d.rdwr(file);
		ziel = fabrik_t::gib_fab( welt, k3d.gib_2d() );
		// freight?
		const char *temp=NULL;
		file->rdwr_str( temp );
		freight = temp ? warenbauer_t::gib_info(temp) : NULL;
		// vehicles
		file->rdwr_str( temp );
		rail_engine = temp ? vehikelbauer_t::gib_info(temp) : NULL;
		file->rdwr_str( temp );
		rail_vehicle = temp ? vehikelbauer_t::gib_info(temp) : NULL;
		file->rdwr_str( temp );
		road_vehicle = temp ? vehikelbauer_t::gib_info(temp) : NULL;
		file->rdwr_str( temp );
		ship_vehicle = temp ? vehikelbauer_t::gib_info(temp) : NULL;
		// ways
		file->rdwr_str( temp );
		rail_weg = temp ? wegbauer_t::gib_besch(temp,0) : NULL;
		file->rdwr_str( temp );
		road_weg = temp ? wegbauer_t::gib_besch(temp,0) : NULL;
	}

	// finally: forbidden connection list
	sint32 cnt = forbidden_conections.count();
	file->rdwr_long(cnt,"Fc");
	if(file->is_saving()) {
		slist_iterator_tpl<fabconnection_t> iter(forbidden_conections);
		while(  iter.next()  ) {
			fabconnection_t fc = iter.get_current();
			fc.rdwr(file);
		}
	}
	else {
		forbidden_conections.clear();
		while(  cnt-->0  ) {
			fabconnection_t fc(0,0,0);
			fc.rdwr(file);
			forbidden_conections.append(fc);
		}
	}
}




void ai_goods_t::fabconnection_t::rdwr(loadsave_t *file)
{
	koord3d k3d;
	if(file->is_saving()) {
		k3d = fab1->gib_pos();
		k3d.rdwr(file);
		k3d = fab2->gib_pos();
		k3d.rdwr(file);
		const char *s = ware->gib_name();
		file->rdwr_str( s );
	}
	else {
		k3d.rdwr(file);
		fab1 = fabrik_t::gib_fab( welt, k3d.gib_2d() );
		k3d.rdwr(file);
		fab2 = fabrik_t::gib_fab( welt, k3d.gib_2d() );
		const char *temp=NULL;
		file->rdwr_str( temp );
		ware = warenbauer_t::gib_info(temp);
	}
}
