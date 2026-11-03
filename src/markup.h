#ifndef OYEZ_MARKUP_H
#define OYEZ_MARKUP_H

#include "core.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  FEED_FORMAT_UNKNOWN = 0,
  FEED_FORMAT_RSS2 = 1,
  FEED_FORMAT_ATOM = 2,
  FEED_FORMAT_RSS1 = 3,
  FEED_FORMAT_JSON = 4
} FeedFormat;

typedef struct {
  char *guid;
  char *title;
  char *url;
  char *author;
  char *summary;
  char *content_html;
  char *published_at;
  char *image_url;
  char **categories;
  size_t category_count;
} ParsedFeedItem;

typedef struct {
  FeedFormat format;
  char *title;
  char *description;
  char *site_url;
  char *language;
  ParsedFeedItem *items;
  size_t item_count;
} ParsedFeed;

typedef struct {
  char *title;
  char *byline;
  char *excerpt;
  char *site_name;
  char *published_time;
  char *lead_image_url;
  char *text_content;
  char *markdown;
  uint32_t word_count;
  uint32_t reading_time_minutes;
} ExtractedContent;

int markup_decode_entities(const char *input, size_t len, Buffer *out);
int markup_strip_html(const char *html, size_t len, Buffer *out);
char *markup_clean_cstr(const char *html);
uint64_t parse_timestamp_ms(const char *str);

int feed_parse(const char *raw, size_t len, const char *base_url, ParsedFeed *out);
void feed_free(ParsedFeed *feed);
const char *feed_format_name(FeedFormat format);

int readability_extract(const char *html, size_t len, const char *url, ExtractedContent *out);
void readability_free(ExtractedContent *content);

#endif
