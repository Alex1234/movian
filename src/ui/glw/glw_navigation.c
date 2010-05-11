/*
 *  GL Widgets, Navigation
 *  Copyright (C) 2008 - 2010 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "glw.h"
#include "glw_event.h"

typedef struct query {
  float x, xmin, xmax;
  float y, ymin, ymax;

  int direction;
  glw_orientation_t orientation;

  glw_t *best;
  float score;

} query_t;


/**
 *
 */
static float
compute_position(glw_t *w, glw_orientation_t o)
{
  glw_t *p, *c;
  float a, x;

  x = 0;

  for(; (p = w->glw_parent) != NULL; w = p) {
    if(p->glw_class->gc_child_orientation != o)
      continue;

    a = w->glw_norm_weight / 2;

    TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
      if(c->glw_flags & GLW_HIDDEN)
	continue;

      if(c == w)
	break;
      a += c->glw_norm_weight;
    }

    x = (2 * a) - 1 + (x * w->glw_norm_weight);
  }
  return x;
}


/**
 *
 */
static void
find_candidate(glw_t *w, query_t *query)
{
  glw_t *c;
  float x, y, distance, dx, dy;

  if(glw_is_focusable(w)) {
    
    x = compute_position(w, GLW_ORIENTATION_HORIZONTAL);
    y = compute_position(w, GLW_ORIENTATION_VERTICAL);

    dx = query->x - x;
    dy = query->y - y;

    if(query->orientation == GLW_ORIENTATION_VERTICAL)
      dx *= 10;
    else
      dy *= 10;

    distance = sqrt(dx * dx + dy * dy);
    if(distance < query->score) {
      query->score = distance;
      query->best = w;
    }
  }

  switch(w->glw_class->gc_nav_descend_mode) {

  case GLW_NAV_DESCEND_ALL:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c->glw_flags & GLW_HIDDEN)
	continue;
      find_candidate(c, query);
    }
    break;
    
  case GLW_NAV_DESCEND_SELECTED:
    if((c = w->glw_selected) != NULL)
      find_candidate(c, query);
    break;

  case GLW_NAV_DESCEND_FOCUSED:
    if(w->glw_focused) {
      c = w->glw_focused;
    } else if(query->direction) {
      c = TAILQ_FIRST(&w->glw_childs);
    } else {
      c = TAILQ_LAST(&w->glw_childs, glw_queue);
    }

    if(c != NULL)
      find_candidate(c, query);
    break;
  }
}

/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e, int local)
{
  glw_t  *p, *c, *t = NULL, *d;
  float x, y;
  int pagemode = 0;
  int pagecnt;
  query_t query;
  int loop = 1;

  x = compute_position(w, GLW_ORIENTATION_HORIZONTAL);
  y = compute_position(w, GLW_ORIENTATION_VERTICAL);

  memset(&query, 0, sizeof(query));

  query.x = x;
  query.y = y;
  query.score = 100000000;

  if(event_is_action(e, ACTION_FOCUS_PREV)) {

    glw_focus_crawl(w, 0);
    event_unref(e);
    return 1;

  } else if(event_is_action(e, ACTION_FOCUS_NEXT)) {

    glw_focus_crawl(w, 1);
    event_unref(e);
    return 1;

  } else if(event_is_action(e, ACTION_UP)) {
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;

  } else if(event_is_action(e, ACTION_PAGE_UP)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;
    pagemode    = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;

  } else if(event_is_action(e, ACTION_TOP)) {
    
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;
    pagemode    = 2;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;


  } else if(event_is_action(e, ACTION_DOWN)) {
    
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_PAGE_DOWN)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;
    pagemode    = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_BOTTOM)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;
    pagemode    = 2;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_LEFT)) {

    query.orientation = GLW_ORIENTATION_HORIZONTAL;
    query.direction   = 0;

    query.xmin = -1;
    query.xmax = x - 0.0001;
    query.ymin = -1;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_RIGHT)) {

    query.orientation = GLW_ORIENTATION_HORIZONTAL;
    query.direction   = 1;

    query.xmin = x + 0.0001;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = 1;

  } else {
    return 0;
  }

  pagecnt = 10;
  c = NULL;
  for(; (p = w->glw_parent) != NULL; w = p) {

    if(local && w->glw_class->gc_flags & GLW_NAVIGATION_SEARCH_BOUNDARY)
      return 0;

    switch(p->glw_class->gc_nav_search_mode) {
    case GLW_NAV_SEARCH_NONE:
      break;

    case GLW_NAV_SEARCH_ARRAY:

      if(query.orientation == GLW_ORIENTATION_VERTICAL
	 && query.direction == 0 && w->glw_flags & GLW_TOP_EDGE)
	break;
      if(query.orientation == GLW_ORIENTATION_VERTICAL
	 && query.direction == 1 && w->glw_flags & GLW_BOTTOM_EDGE)
	break;
      if(query.orientation == GLW_ORIENTATION_HORIZONTAL &&
	 query.direction == 0 && w->glw_flags & GLW_LEFT_EDGE)
	break;
      if(query.orientation == GLW_ORIENTATION_HORIZONTAL &&
	 query.direction == 1 && w->glw_flags & GLW_RIGHT_EDGE)
	break;

      if(query.orientation == GLW_ORIENTATION_VERTICAL) {

	int xentries = glw_array_get_xentries(p);

	if(pagemode == 0) {
	  pagemode = 1;
	  pagecnt = xentries;
	} else if(pagemode == 1) {
	  pagecnt *= xentries;
	}
      }
      goto container;


    case GLW_NAV_SEARCH_BY_ORIENTATION:
      if(pagemode)
	break;
      /* FALLTHROUGH */
    case GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING:
      if(p->glw_class->gc_child_orientation != query.orientation)
	break;
    container:
      c = w;
      loop = 1;
      while(loop) {
	if(query.direction == 1) {
	  /* Down / Right */
	  if(pagemode == 1) {

	    d = glw_next_widget(c);
	    if(d != NULL) {
	      while(pagecnt--) {
		c = d;
		d = glw_next_widget(c);
		if(d == NULL) {
		  loop = 0;
		  break;
		}
	      }
	    } else {
	      loop = 0;
	    }

	  } else if(pagemode == 2) {

	    c = glw_last_widget(p);

	  } else {
	    c = glw_next_widget(c);
	  }

	} else {
	  /* Up / Left */
	  if(pagemode == 1) {

	    d = glw_prev_widget(c);
	    if(d != NULL) {

	      while(pagecnt--) {
		c = d;
		d = glw_prev_widget(c);
		if(d == NULL) {
		  loop = 0;
		  break;
		}
	      }
	    } else {
	      loop = 0;
	    }

	  } else if(pagemode == 2) {
	    
	    c = glw_first_widget(p);

	  } else {
	    c = glw_prev_widget(c);
	  }

	}

	if(c == NULL)
	  break;
	find_candidate(c, &query);
      }
      break;
    }
  }

  t = query.best;


  if(t != NULL) {
    glw_focus_set(t->glw_root, t, 1);
    event_unref(e);
    return 1;
  }

  return 0;
}


