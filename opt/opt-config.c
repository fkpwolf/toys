#include "opt.h"
#include <stdio.h> 		/* for scanf */
#include <string.h> 		/* for strcmp */

/* 
  <opt>
  <defaults>
    <bullet font="" color="">
    <title font="" color="">
    <offsets border="" title-spacing="" bullet-border="" bullet-spacing=""/>
    <background img="" | color= "" />
    <transition style="cube|flip|fade" />
  </defualts>
  <slide>
    <background img="" | color= "" />
    <title font="" color=""></title>
    <bullet font="" color="" symbol="none"></bullet>
    <img src="" />
    <code width="xx"></code>
    <transition style="cube|flip|fade" />
  </slide>
  <transition type=""/>
  ....

  </opt>
 */

typedef struct OptParseInfo OptParseInfo;

typedef enum 
{
  INITIAL,
  IN_OPT,
  IN_DEFAULTS,
  IN_DEFAULTS_TITLE,
  IN_DEFAULTS_BULLET,
  IN_DEFAULTS_TRANS,
  IN_DEFAULTS_BG,
  IN_FONTS,
  IN_OFFSETS,
  IN_SLIDE,
  IN_TITLE,
  IN_BULLET,
  IN_TRANS,
  IN_IMG,
  IN_BG,
  FINAL
} 
OptParseState;


typedef enum 
{
  TAG_UNKNOWN = 0,
  TAG_OPT,
  TAG_DEFAULTS,
  TAG_DEFAULTS_TITLE,
  TAG_DEFAULTS_BULLET,
  TAG_DEFAULTS_TRANS,
  TAG_DEFAULTS_BG,
  TAG_SLIDE,
  TAG_TITLE,
  TAG_BULLET,
  TAG_TRANS,
  TAG_IMG,
  TAG_BG

} OptParseTag;

const struct { gchar *name; OptTransitionStyle style; } _style_lookup[] = 
  {
    { "cube", OPT_TRANSITION_CUBE },
    { "page", OPT_TRANSITION_PAGE },
    { "flip", OPT_TRANSITION_FLIP },
    { "zoom", OPT_TRANSITION_ZOOM },
    { "yzflip", OPT_TRANSITION_YZ_FLIP },
    { "fade", OPT_TRANSITION_FADE },
    { NULL, 0 }
  };


struct OptParseInfo
{
  OptShow           *show;
  OptParseState      state;
  OptSlide          *slide;

  GdkPixbuf *default_bg;

  GString           *title_buf;
  gchar             *title_font;
  ClutterColor       title_color;
  ClutterColor       title_default_color;

  GString           *bullet_buf;
  gchar             *bullet_font;
  ClutterColor       bullet_color;
  ClutterColor       bullet_default_color;
  OptSlideBulletSymbol bullet_sym;

  OptTransitionStyle style_default;
};

static void 
color_from_string (const gchar *spec, ClutterColor *color)
{
  if (spec[0] == '#' && strlen(spec) == 9)
    {
      guint32 result;
      if (sscanf (spec+1, "%x", &result))
	{
	  color->red   = result >> 24 & 0xff;
	  color->green = (result >> 16) & 0xff;
	  color->blue  = (result >> 8) & 0xff;
	  color->alpha = result & 0xff;
	  return;
	}
    }
  
  g_warning("unable to parse '%s' as a color in format #RRGGBBAA", spec);
}

static OptTransitionStyle
lookup_style (const gchar *name)
{
  gint i = 0;

  while (_style_lookup[i].name != NULL)
    {
      if (!strcmp(name, _style_lookup[i].name))
	return _style_lookup[i].style;

      i++;
    }

  return OPT_TRANSITION_ANY;
}

static int
expect_tag (GMarkupParseContext *context,
	    const gchar         *actor_name,
	    GError             **error,
	    ...)
{
  va_list vap;
  const char *expected;
  int n_expected = 0;
  
  va_start (vap, error);
  expected = va_arg (vap, const char *);
  while (expected)
    {
      int value = va_arg (vap, int);
      n_expected++;
      
      if (strcmp (expected, actor_name) == 0)
	return value;
      
      expected = va_arg (vap, const char *);
    }
  
  va_end (vap);

  if (n_expected == 0)
    {
      g_set_error (error,
		   G_MARKUP_ERROR,
		   G_MARKUP_ERROR_INVALID_CONTENT,
		   "Unexpected tag '%s', no tags expected",
		   actor_name);
    }
  else
    {
      GString *tag_string = g_string_new (NULL);

      va_start (vap, error);
      expected = va_arg (vap, const char *);
      while (expected)
	{
	  va_arg (vap, int);

	  if (tag_string->len)
	    g_string_append (tag_string, ", ");
	  g_string_append (tag_string, expected);
	    
	  expected = va_arg (vap, const char *);
	}
  
      va_end (vap);

      if (n_expected == 1)
	g_set_error (error,
		     G_MARKUP_ERROR,
		     G_MARKUP_ERROR_INVALID_CONTENT,
		     "Unexpected tag '%s', expected '%s'",
		     actor_name, tag_string->str);
      else
	g_set_error (error,
		     G_MARKUP_ERROR,
		     G_MARKUP_ERROR_INVALID_CONTENT,
		     "Unexpected tag '%s', expected one of: %s",
		     actor_name, tag_string->str);

      g_string_free (tag_string, TRUE);
    }
  
  return 0;
}

static gboolean
extract_attrs (GMarkupParseContext *context,
	       const gchar        **attribute_names,
	       const gchar        **attribute_values,
	       GError             **error,
	       ...)
{
  va_list vap;
  const char *name;
  gboolean *attr_map;
  gboolean nattrs = 0;
  int i;

  for (i = 0; attribute_names[i]; i++)
    nattrs++;

  attr_map = g_new0 (gboolean, nattrs);

  va_start (vap, error);
  name = va_arg (vap, const char *);
  while (name)
    {
      gboolean mandatory = va_arg (vap, gboolean);
      const char **loc = va_arg (vap, const char **);
      gboolean found = FALSE;

      for (i = 0; attribute_names[i]; i++)
	{
	  if (!attr_map[i] && strcmp (attribute_names[i], name) == 0)
	    {
	      if (found)
		{
		  g_set_error (error,
			       G_MARKUP_ERROR,
			       G_MARKUP_ERROR_INVALID_CONTENT,
			       "Duplicate attribute '%s'", name);
		  return FALSE;
		}
	        
	      *loc = attribute_values[i];
	      found = TRUE;
	      attr_map[i] = TRUE;
	    }
	}
      
      if (!found && mandatory)
	{
	  g_set_error (error,
		       G_MARKUP_ERROR,
		       G_MARKUP_ERROR_INVALID_CONTENT,
		       "Missing attribute '%s'", name);
	  return FALSE;
	}
      
      name = va_arg (vap, const char *);
    }

  for (i = 0; i < nattrs; i++)
    if (!attr_map[i])
      {
	g_set_error (error,
		     G_MARKUP_ERROR,
		     G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
		     "Unknown attribute '%s'", attribute_names[i]);
	return FALSE;
      }

  return TRUE;
}


static void 
opt_parse_on_start_actor (GMarkupParseContext *context,
			    const gchar         *actor_name,
			    const gchar        **attr_names,
			    const gchar        **attr_values,
			    gpointer             user_data,
			    GError             **error)
{
  OptParseTag   tag;
  OptParseInfo *info = user_data;

  switch (info->state)
    {
    case INITIAL:
      if (expect_tag (context, actor_name, error, "opt", TAG_OPT, NULL) 
	  && extract_attrs (context, attr_names, attr_values, error, NULL))
	info->state = IN_OPT;
      break;

      /***** Top level, just defaults and slide *****/

    case IN_OPT:
      tag = expect_tag (context, actor_name, error, 
			"defaults", TAG_DEFAULTS, 
			"slide", TAG_SLIDE,
			NULL); 
      switch (tag)
	{
	case TAG_DEFAULTS:
	  info->state = IN_DEFAULTS;
	  break;
	case TAG_SLIDE:
	  {
	    OptTransition *trans;

	    info->state = IN_SLIDE;
	    info->slide = opt_slide_new (info->show);

	    g_object_set (info->show, "background", info->default_bg, NULL);

	    trans = opt_transition_new (info->style_default); 
	    opt_transition_set_from (trans, info->slide);
	    opt_slide_set_transition (info->slide, trans);
	  }
	  break;
	default:
	  break;
	}
      break;

      /*****  Default tags *****/

    case IN_DEFAULTS:
      tag = expect_tag (context, actor_name, error, 
			"title",  TAG_DEFAULTS_TITLE, 
			"bullet", TAG_DEFAULTS_BULLET,
			"transition", TAG_DEFAULTS_TRANS,
			"background", TAG_DEFAULTS_BG,
			NULL); 
      switch (tag)
	{
	case TAG_DEFAULTS_TRANS:
	  {
	    const char *style_str = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "style", FALSE, &style_str,
			       NULL))
	      {
		info->style_default = lookup_style (style_str);
	      }
	  }
	  info->state = IN_DEFAULTS_TRANS;
	  break;
	case TAG_DEFAULTS_TITLE:
	  {
	    const char *color = NULL;
	    const char *font = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "font", FALSE, &font,
			       "color",  FALSE, &color,
			       NULL))
	      {
		if (font)
		  g_object_set(info->show,
			       "title-font", font,
			       NULL);

		if (color)
		  {
		    color_from_string (color, &info->title_default_color);
		  }
	      }
	  }
	  info->state = IN_DEFAULTS_TITLE;
	  break;

	case TAG_DEFAULTS_BULLET:
	  {
	    const char *color = NULL;
	    const char *font = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "font", FALSE, &font,
			       "color",  FALSE, &color,

			       NULL))
	      {
		if (font)
		  g_object_set(info->show,
			       "bullet-font", font,
			       NULL);

		if (color)
		  {
		    color_from_string (color, &info->bullet_default_color);

		    opt_show_set_bullet_color (info->show,
					       &info->bullet_default_color);
		  }
	      }
	  }
	  info->state = IN_DEFAULTS_BULLET;
	  break;
	case TAG_DEFAULTS_BG:
	  {
	    const char *src = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "src", TRUE, &src,
			       NULL))
	      {
		GdkPixbuf *pic;
		
		pic = gdk_pixbuf_new_from_file_at_size (src,
                                                        CLUTTER_STAGE_WIDTH(),
                                                        CLUTTER_STAGE_HEIGHT(),
                                                        NULL);

		if (pic == NULL)
		  {
		    g_set_error (error,
				 G_MARKUP_ERROR,
				 G_MARKUP_ERROR_INVALID_CONTENT,
				 "Unable to load '%s'", src);
		  }
		
		info->default_bg = pic;
	      }
	  }
	  info->state = IN_DEFAULTS_BG;
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	}
      
      break;

      /*****  Slide Tags *****/

    case IN_SLIDE:
      tag = expect_tag (context, actor_name, error, 
			"title",      TAG_TITLE, 
			"bullet",     TAG_BULLET,
			"img",        TAG_IMG,
			"transition", TAG_TRANS,
			"background", TAG_BG,
			NULL); 
      switch (tag)
	{
	case TAG_BG:
	  {
	    const char *src = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "src", TRUE, &src,
			       NULL))
	      {
		GdkPixbuf *pic = NULL;

		pic = gdk_pixbuf_new_from_file_at_size (src,
                                                        CLUTTER_STAGE_WIDTH(),
                                                        CLUTTER_STAGE_HEIGHT(),
                                                        NULL);

		if (pic == NULL)
		  {
		    g_set_error (error,
				 G_MARKUP_ERROR,
				 G_MARKUP_ERROR_INVALID_CONTENT,
				 "Unable to load '%s'", src);
		  }
		
		opt_slide_set_background_pixbuf (info->slide, pic);

		g_object_unref (pic);
	      }
	    info->state = IN_BG;
	  }
	  break;
	case TAG_TRANS:
	  {
	    const char *style_str = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "style", TRUE, &style_str,
			       NULL))
	      {
		OptTransitionStyle style;
		OptTransition     *trans;

		style = lookup_style (style_str);
		
		trans = opt_slide_get_transition (info->slide);
		opt_transition_set_style (trans, style);
	      }
	    info->state = IN_TRANS;
	  }
	  break;
	case TAG_IMG:
	  {
	    gchar *img_path = NULL;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "src", TRUE, &img_path,
			       NULL))
	      {
		GdkPixbuf    *pix = NULL;
		ClutterActor *pic = NULL;

		pix = gdk_pixbuf_new_from_file (img_path, NULL);

		if (pix == NULL)
		  {
		    g_set_error (error,
				 G_MARKUP_ERROR,
				 G_MARKUP_ERROR_INVALID_CONTENT,
				 "Unable to load '%s'", img_path);
		  }
		else
		  {
		    pic = clutter_texture_new ();
                    clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (pic),
                                     gdk_pixbuf_get_pixels (pix),
                                     gdk_pixbuf_get_has_alpha (pix),
                                     gdk_pixbuf_get_width (pix),
                                     gdk_pixbuf_get_height (pix),
                                     gdk_pixbuf_get_rowstride (pix),
                                     4, 0,
                                     NULL);

		    opt_slide_add_bullet (info->slide, pic);
		  }
	      }
	    info->state = IN_IMG;
	  }
	  break;

	case TAG_TITLE:
	  {
	    const char *color = NULL;
	    const char *font = NULL;

	    info->state      = IN_TITLE;
	    info->title_buf  = g_string_new("");
	    info->title_font = NULL;
	    info->title_color = info->title_default_color;

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "font", FALSE, &font,
			       "color",  FALSE, &color,
			       NULL))
	      {
		if (font)
		  info->title_font = g_strdup(font);

		if (color)
		  color_from_string(color, &info->title_color);
		  
	      }
	  }
	  break;

	case TAG_BULLET:
	  {
	    const char *color = NULL;
	    const char *font = NULL;
	    const char *sym  = NULL;
	    
	    info->state        = IN_BULLET;
	    info->bullet_buf   = g_string_new("");
	    info->bullet_font  = NULL;
	    info->bullet_color = info->bullet_default_color;
	    info->bullet_sym   = OPT_BULLET_REGULAR;	    

	    if (extract_attrs (context, attr_names, attr_values, error,
			       "font", FALSE, &font,
			       "color",  FALSE, &color,
			       "symbol", FALSE, &sym,
			       NULL))
	      {
		if (font)
		  info->bullet_font = g_strdup(font);
		
		if (color)
		  color_from_string(color, &info->bullet_color);
		
		if (sym && !strcmp(sym, "none"))
		  info->bullet_sym = OPT_BULLET_NONE;
	      }
	  }
	  break;
	default:
	  break;
	}
    default:
      break;
    }      
}

static void 
opt_parse_on_end_actor (GMarkupParseContext *context,
			  const gchar         *actor_name,
			  gpointer             user_data,
			  GError             **error)
{
  OptParseInfo *info = user_data;

  switch (info->state)
    {
    case INITIAL:
      g_assert_not_reached ();
      break;
    case IN_OPT:
      info->state = FINAL;
      break;
    case IN_SLIDE:
      opt_show_add_slide (info->show, info->slide);
      info->state = IN_OPT;
      info->slide = NULL;
      break;
    case IN_DEFAULTS:
      info->state = IN_OPT;
      break;
    case IN_DEFAULTS_TITLE:
    case IN_DEFAULTS_BULLET:
    case IN_DEFAULTS_TRANS:
    case IN_DEFAULTS_BG:
      info->state = IN_DEFAULTS;
      break;
    case IN_BG:
    case IN_IMG:
      info->state = IN_SLIDE;
      break;
    case IN_TITLE:
      opt_slide_set_title (info->slide, 
			   info->title_buf->str,
			   info->title_font,
			   &info->title_color);
      g_string_free (info->title_buf, TRUE);

      if (info->title_font)
	g_free (info->title_font);
      info->title_font  = NULL;
      info->bullet_buf  = NULL;
      info->state       = IN_SLIDE;
      break;
    case IN_BULLET:
      opt_slide_add_bullet_text_item (info->slide, 
				      info->bullet_buf->str,
				      info->bullet_font,
				      info->bullet_sym,
				      &info->bullet_color);
      g_string_free (info->bullet_buf, TRUE);
      if (info->bullet_font)
	g_free (info->bullet_font);
      info->bullet_font  = NULL;
      info->bullet_buf = NULL;
      info->state = IN_SLIDE;
      break;
    case IN_TRANS:
      info->state = IN_SLIDE;
      break;
    case FINAL:
      g_assert_not_reached ();
      break;
    default:
      break;
    }
}


static void 
opt_parse_on_text (GMarkupParseContext *context,
		   const gchar         *text,
		   gsize                text_len,  
		   gpointer             user_data,
		   GError             **error)
{
  int           i;
  OptParseInfo *info = user_data;

  switch (info->state)
    {
    case IN_TITLE:
      g_string_append_len (info->title_buf, text, text_len);
      break;
    case IN_BULLET:
      g_string_append_len (info->bullet_buf, text, text_len);
      break;
    case INITIAL:
    case IN_IMG:
    case IN_OPT:
    case IN_DEFAULTS:
    case IN_DEFAULTS_TITLE:
    case IN_DEFAULTS_BULLET:
    case IN_DEFAULTS_TRANS:
    case IN_DEFAULTS_BG:
    case IN_FONTS:
    case IN_OFFSETS:
    case IN_SLIDE:
    case IN_BG:
    case IN_TRANS:
    case FINAL:
      for (i = 0; i < text_len; i++)
	if (!g_ascii_isspace (text[i]))
	  {
	    g_set_error (error,
			 G_MARKUP_ERROR,
			 G_MARKUP_ERROR_INVALID_CONTENT,
			 "Unexpected text '%s' in presentation file",
			 text);
	    return;
	  }
      break;
    }
}


gboolean
opt_config_load (OptShow     *show, 
		 const gchar *filename,
		 GError     **error)
{
  GMarkupParseContext *context;
  OptParseInfo         info;
  char                *contents;
  gsize                len;
  gboolean             result;

  const GMarkupParser parser = 
    {
      opt_parse_on_start_actor,
      opt_parse_on_end_actor,
      opt_parse_on_text,
      NULL,
      NULL
    };

  memset (&info, 0, sizeof(OptParseInfo));

  info.state = INITIAL; 
  info.show  = show;
  /*
  info.bullet_default_color = { 0, 0, 0, 0xff };
  info.title_default_color  = { 0, 0, 0, 0xff };
  */
  info.style_default        = OPT_TRANSITION_FADE;

  if (!g_file_get_contents (filename, &contents, &len, error))
    return FALSE;

  context = g_markup_parse_context_new (&parser, 0, &info, NULL);
  result  = g_markup_parse_context_parse (context, contents, len, error);

  return result;
}
