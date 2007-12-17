/*
 * Copyright (c) 1997 - 2003 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project and may not be used
 * in other projects without written permission of the author.
 */

#include <algorithm>
#include "citylist_stats_t.h"
#include "../simgraph.h"
#include "../simcolor.h"
#include "../simwin.h"
#include "../simworld.h"
#include "../simskin.h"
#include "../besch/skin_besch.h"
#include "../gui/stadt_info.h"
#include "../dataobj/translator.h"
#include "../utils/cbuffer_t.h"
#include <string.h>

static const char* total_bev_translation = NULL;
char citylist_stats_t::total_bev_string[128];


citylist_stats_t::citylist_stats_t(karte_t* w, citylist::sort_mode_t sortby, bool sortreverse) :
	welt(w)
{
	setze_groesse(koord(210, welt->gib_staedte().get_count() * (LINESPACE + 1) - 10));
	total_bev_translation = translator::translate("Total inhabitants:");
	sort(sortby, sortreverse);
	line_select = 0xFFFFFFFFu;
}


class compare_cities
{
	public:
		compare_cities(citylist::sort_mode_t sortby_, bool reverse_) :
			sortby(sortby_),
			reverse(reverse_)
		{}

		bool operator ()(const stadt_t* a, const stadt_t* b)
		{
			int cmp;
			switch (sortby) {
				default: NOT_REACHED
				case citylist::by_name:   cmp = strcmp(a->gib_name(), b->gib_name());    break;
				case citylist::by_size:   cmp = a->gib_einwohner() - b->gib_einwohner(); break;
				case citylist::by_growth: cmp = a->gib_wachstum()  - b->gib_wachstum();  break;
			}
			return reverse ? cmp > 0 : cmp < 0;
		}

	private:
		citylist::sort_mode_t sortby;
		bool reverse;
};


void citylist_stats_t::sort(citylist::sort_mode_t sortby, bool sortreverse)
{
	const weighted_vector_tpl<stadt_t*>& cities = welt->gib_staedte();

	city_list.clear();
	city_list.resize(cities.get_count());

	for (weighted_vector_tpl<stadt_t*>::const_iterator i = cities.begin(), end = cities.end(); i != end; ++i) {
		city_list.push_back(*i);
	}
	std::sort(city_list.begin(), city_list.end(), compare_cities(sortby, sortreverse));
}


void citylist_stats_t::infowin_event(const event_t * ev)
{
	const uint line = ev->cy / (LINESPACE + 1);

	line_select = 0xFFFFFFFFu;
	if (line >= city_list.get_count()) return;

	stadt_t* stadt = city_list[line];
	if(  ev->button_state>0  &&  ev->cx>0  &&  ev->cx<15  ) {
		line_select = line;
	}

	if (IS_LEFTRELEASE(ev) && ev->cy>0) {
		if(ev->cx>0  &&  ev->cx<15) {
			const koord pos = stadt->gib_pos();
			welt->change_world_position( koord3d(pos, welt->min_hgt(pos)) );
		}
		else {
			stadt->zeige_info();
		}
	} else if (IS_RIGHTRELEASE(ev) && ev->cy > 0) {
		const koord pos = stadt->gib_pos();
		welt->change_world_position( koord3d(pos, welt->min_hgt(pos)) );
	}
}


void citylist_stats_t::zeichnen(koord offset)
{
	image_id const arrow_right_normal = skinverwaltung_t::window_skin->gib_bild(10)->gib_nummer();
	sint32 total_bev = 0;
	sint32 total_growth = 0;

	for (uint32 i = 0; i < city_list.get_count(); i++) {
		const stadt_t* stadt = city_list[i];
		sint32 bev = stadt->gib_einwohner();
		sint32 growth = stadt->gib_wachstum();

		char buf[256];
		sprintf( buf, "%s: %i (%+.1f)", stadt->gib_name(), bev, growth/10.0 );
		display_proportional_clip(offset.x + 4 + 10, offset.y + i * (LINESPACE + 1), buf, ALIGN_LEFT, COL_BLACK, true);

		if(i!=line_select) {
			// goto information
			display_color_img(arrow_right_normal, offset.x + 2, offset.y + i * (LINESPACE + 1), 0, false, true);
		}
		else {
			// select goto button
			display_color_img(skinverwaltung_t::window_skin->gib_bild(11)->gib_nummer(),
				offset.x + 2, offset.y + i * (LINESPACE + 1), 0, false, true);
		}

		total_bev    += bev;
		total_growth += growth;
	}
	// some cities there?
	if (total_bev > 0) {
		sprintf(total_bev_string,"%s %d (%+.1f)", total_bev_translation, total_bev, total_growth/10.0 );
	} else {
		total_bev_string[0] = 0;
	}
}
