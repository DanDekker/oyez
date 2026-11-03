#ifndef OYEZ_CATALOG_H
#define OYEZ_CATALOG_H

#include "ontology.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *title;
  const char *feed_url;
  const char *site_url;
  const char *category;
  WorldRegion region;
  const char *country;
  const char *subregion;
  const char *language;
  const char *source_tier;
  const char *editorial_focus;
} CatalogEntry;

const CatalogEntry *catalog_entries(size_t *out_count);

#endif
