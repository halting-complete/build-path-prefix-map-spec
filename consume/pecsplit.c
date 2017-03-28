#include "prefix_map.h"

/** Parsing the variable. */
/* For Applying the variable, and Main program, see prefix_map.h. */

/* Parse a single part of a single prefix-map pair.

   Returns 0 on failure and 1 on success.  */
int
prefix_map_parse_unquote (char *src)
{
  for (char *dest = src; 0 != (*dest = *src); ++dest, ++src)
    switch (*src)
      {
      case ':':
      case '=':
	return 0; // should have been escaped
      case '%':
	switch (*(src + 1))
	  {
	  case '.':
	    *dest = ':';
	    goto unquoted;
	  case '+':
	    *dest = '=';
	  unquoted:
	  case '#':
	    ++src;
	    break;
	  default:
	    return 0; // invalid
	  }
      }
  return 1;
}

/* Parse a single prefix-map.

   Returns 0 on failure and 1 on success.  */
int
prefix_map_parse1 (struct prefix_map **map_head, char *arg)
{
  char *p;
  p = strchr (arg, '=');
  if (!p)
    return 0;
  *p = '\0';
  if (!prefix_map_parse_unquote (arg))
    return 0;
  p++;
  if (!prefix_map_parse_unquote (p))
    return 0;

  return prefix_map_push (map_head, arg, p);
}

/* Parse a prefix-map according to the BUILD_PATH_PREFIX_MAP standard.

   The input string value is of the form

     dst[0]=src[0]:dst[1]=src[1]...

   Every dst[i] and src[i] has had "%", "=" and ":" characters replaced with
   "%#", "%+", and "%." respectively; this function reverses this replacement.

   Rightmost entries are stored at the head of the parsed structure.

   Returns 0 on failure and 1 on success.  */
int
prefix_map_parse (struct prefix_map **map_head, const char *arg)
{
  struct prefix_map *old_head = *map_head;

  size_t len = strlen (arg);
  char *copy = (char *) alloca (len + 1);
  memcpy (copy, arg, len + 1); // strtok modifies the string so we have to copy it

  const char *sep = ":";
  char *end, *tok = strtok_r (copy, sep, &end);
  while (tok != NULL)
    {
      if (!prefix_map_parse1 (map_head, tok))
	{
	  fprintf (stderr, "invalid value for prefix-map: '%s'; rewinding to: %p\n", arg, old_head);
	  prefix_map_pop_until (map_head, old_head);
	  return 0;
	}

      tok = strtok_r (NULL, sep, &end);
    }

  return 1;
}

int
main (int argc, char *argv[])
{
  return generic_main (prefix_map_parse, argc, argv);
}
