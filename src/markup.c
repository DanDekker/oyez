#include "markup.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ci_starts_with(const char *s, size_t len, const char *prefix) {
  size_t plen = strlen(prefix);
  if(len < plen) return 0;
  for(size_t i = 0; i < plen; ++i) {
    if(tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return 0;
  }
  return 1;
}

static int ci_equals(const char *a, size_t alen, const char *b) {
  size_t blen = strlen(b);
  if(alen != blen) return 0;
  return ci_starts_with(a, alen, b);
}

static const char *ci_find(const char *haystack, size_t hlen, const char *needle) {
  size_t nlen = strlen(needle);
  if(nlen == 0) return haystack;
  if(hlen < nlen) return NULL;
  for(size_t i = 0; i + nlen <= hlen; ++i) {
    if(ci_starts_with(haystack + i, hlen - i, needle)) return haystack + i;
  }
  return NULL;
}

static int append_utf8_codepoint(Buffer *out, uint32_t cp) {
  if(cp == 0 || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) return 0;
  char u[4];
  size_t n = 0;
  if(cp <= 0x7Fu) {
    u[0] = (char)cp;
    n = 1u;
  } else if(cp <= 0x7FFu) {
    u[0] = (char)(0xC0u | ((cp >> 6u) & 0x1Fu));
    u[1] = (char)(0x80u | (cp & 0x3Fu));
    n = 2u;
  } else if(cp <= 0xFFFFu) {
    u[0] = (char)(0xE0u | ((cp >> 12u) & 0x0Fu));
    u[1] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
    u[2] = (char)(0x80u | (cp & 0x3Fu));
    n = 3u;
  } else {
    u[0] = (char)(0xF0u | ((cp >> 18u) & 0x07u));
    u[1] = (char)(0x80u | ((cp >> 12u) & 0x3Fu));
    u[2] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
    u[3] = (char)(0x80u | (cp & 0x3Fu));
    n = 4u;
  }
  buffer_append(out, u, n);
  return 0;
}

typedef struct {
  const char *name;
  const char *val;
} EntityMap;

static const EntityMap k_entities[] = {
    {"amp", "&"},       {"lt", "<"},        {"gt", ">"},        {"quot", "\""},
    {"apos", "'"},      {"#39", "'"},       {"nbsp", " "},      {"ndash", "-"},
    {"mdash", "--"},    {"lsquo", "'"},     {"rsquo", "'"},     {"ldquo", "\""},
    {"rdquo", "\""},    {"hellip", "..."},  {"copy", "(c)"},    {"reg", "(R)"},
    {"trade", "(TM)"},  {"euro", "EUR "},   {"pound", "GBP "},  {"yen", "JPY "}
};

int markup_decode_entities(const char *input, size_t len, Buffer *out) {
  if(!out) return -1;
  if(!input || len == 0) return 0;

  size_t i = 0;
  while(i < len) {
    if(input[i] == '&') {
      size_t semi = i + 1u;
      while(semi < len && semi - i <= 12u && input[semi] != ';' && input[semi] != '&' &&
            !isspace((unsigned char)input[semi])) {
        semi++;
      }
      if(semi < len && input[semi] == ';' && semi > i + 1u) {
        const char *ent = input + i + 1u;
        size_t elen = semi - (i + 1u);
        if(elen > 1u && ent[0] == '#') {
          uint32_t cp = 0;
          int valid = 1;
          if(ent[1] == 'x' || ent[1] == 'X') {
            if(elen <= 2u) valid = 0;
            for(size_t k = 2u; k < elen; ++k) {
              char c = ent[k];
              uint32_t d = 0;
              if(c >= '0' && c <= '9') d = (uint32_t)(c - '0');
              else if(c >= 'a' && c <= 'f') d = 10u + (uint32_t)(c - 'a');
              else if(c >= 'A' && c <= 'F') d = 10u + (uint32_t)(c - 'A');
              else {
                valid = 0;
                break;
              }
              cp = (cp << 4u) | d;
              if(cp > 0x10FFFFu) {
                valid = 0;
                break;
              }
            }
          } else {
            for(size_t k = 1u; k < elen; ++k) {
              char c = ent[k];
              if(c < '0' || c > '9') {
                valid = 0;
                break;
              }
              cp = cp * 10u + (uint32_t)(c - '0');
              if(cp > 0x10FFFFu) {
                valid = 0;
                break;
              }
            }
          }
          if(valid && cp > 0 && !(cp >= 0xD800u && cp <= 0xDFFFu) && cp <= 0x10FFFFu) {
            append_utf8_codepoint(out, cp);
            i = semi + 1u;
            continue;
          }
        } else {
          int matched = 0;
          for(size_t m = 0; m < sizeof(k_entities) / sizeof(k_entities[0]); ++m) {
            if(ci_equals(ent, elen, k_entities[m].name)) {
              buffer_text(out, k_entities[m].val);
              matched = 1;
              break;
            }
          }
          if(matched) {
            i = semi + 1u;
            continue;
          }
        }
      }
    }
    buffer_append(out, input + i, 1u);
    i++;
  }
  return 0;
}

static int append_normalized_ws(Buffer *out, const char *s, size_t len) {
  int prev_space = 1;
  for(size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)s[i];
    if(isspace(c)) {
      if(!prev_space) {
        buffer_append(out, " ", 1u);
        prev_space = 1;
      }
    } else {
      buffer_append(out, (const char *)&c, 1u);
      prev_space = 0;
    }
  }
  while(out->len > 0 && out->data[out->len - 1u] == ' ') {
    out->len--;
    out->data[out->len] = '\0';
  }
  return 0;
}

int markup_strip_html(const char *html, size_t len, Buffer *out) {
  if(!out) return -1;
  if(!html || len == 0) return 0;

  Buffer raw;
  buffer_init(&raw);
  size_t i = 0;
  while(i < len) {
    if(i + 9u <= len && memcmp(html + i, "<![CDATA[", 9u) == 0) {
      const char *end = ci_find(html + i + 9u, len - (i + 9u), "]]>");
      size_t cdata_len = end ? (size_t)(end - (html + i + 9u)) : (len - (i + 9u));
      Buffer sub;
      buffer_init(&sub);
      markup_strip_html(html + i + 9u, cdata_len, &sub);
      if(sub.len > 0) {
        buffer_append(&raw, " ", 1u);
        buffer_append(&raw, sub.data, sub.len);
        buffer_append(&raw, " ", 1u);
      }
      buffer_free(&sub);
      if(!end) break;
      i = (size_t)((end + 3u) - html);
      continue;
    }
    if(i + 4u <= len && memcmp(html + i, "<!--", 4u) == 0) {
      const char *end = ci_find(html + i + 4u, len - (i + 4u), "-->");
      if(!end) break;
      i = (size_t)((end + 3u) - html);
      continue;
    }
    if(html[i] == '<') {
      if(ci_starts_with(html + i, len - i, "<script")) {
        const char *end = ci_find(html + i + 7u, len - (i + 7u), "</script>");
        if(!end) break;
        i = (size_t)((end + 9u) - html);
        continue;
      }
      if(ci_starts_with(html + i, len - i, "<style")) {
        const char *end = ci_find(html + i + 6u, len - (i + 6u), "</style>");
        if(!end) break;
        i = (size_t)((end + 8u) - html);
        continue;
      }
      size_t close = i + 1u;
      while(close < len && html[close] != '>') close++;
      if(close < len) {
        size_t tlen = close - (i + 1u);
        const char *ts = html + i + 1u;
        if(tlen > 0 && ts[0] == '/') {
          ts++;
          tlen--;
        }
        int is_inline = (ci_equals(ts, tlen, "b") || ci_equals(ts, tlen, "i") ||
                         ci_equals(ts, tlen, "strong") || ci_equals(ts, tlen, "em") ||
                         ci_equals(ts, tlen, "span") || ci_equals(ts, tlen, "u") ||
                         ci_equals(ts, tlen, "code") || ci_equals(ts, tlen, "sub") ||
                         ci_equals(ts, tlen, "sup") || ci_starts_with(ts, tlen, "a "));
        if(!is_inline) {
          buffer_append(&raw, " ", 1u);
        }
        i = close + 1u;
        continue;
      }
    }
    buffer_append(&raw, html + i, 1u);
    i++;
  }

  Buffer decoded;
  buffer_init(&decoded);
  if(markup_decode_entities(raw.data ? raw.data : "", raw.len, &decoded) != 0) {
    buffer_free(&raw);
    buffer_free(&decoded);
    return -1;
  }
  buffer_free(&raw);

  int rc = append_normalized_ws(out, decoded.data ? decoded.data : "", decoded.len);
  buffer_free(&decoded);
  return rc;
}

char *markup_clean_cstr(const char *html) {
  if(!html) {
    char *empty = malloc(1u);
    if(empty) empty[0] = '\0';
    return empty;
  }
  Buffer out;
  buffer_init(&out);
  if(markup_strip_html(html, strlen(html), &out) != 0) {
    buffer_free(&out);
    return NULL;
  }
  if(!out.data) {
    char *empty = malloc(1u);
    if(empty) empty[0] = '\0';
    return empty;
  }
  return out.data;
}

static char *dup_span_clean(const char *s, size_t len) {
  Buffer out;
  buffer_init(&out);
  if(markup_strip_html(s, len, &out) != 0) {
    buffer_free(&out);
    return NULL;
  }
  if(!out.data) {
    char *empty = malloc(1u);
    if(empty) empty[0] = '\0';
    return empty;
  }
  return out.data;
}

static char *dup_span_raw_trimmed(const char *s, size_t len) {
  while(len > 0 && isspace((unsigned char)*s)) {
    s++;
    len--;
  }
  while(len > 0 && isspace((unsigned char)s[len - 1u])) {
    len--;
  }
  if(len >= 12u && memcmp(s, "<![CDATA[", 9u) == 0 && memcmp(s + len - 3u, "]]>", 3u) == 0) {
    s += 9u;
    len -= 12u;
  }
  Buffer out;
  buffer_init(&out);
  if(markup_decode_entities(s, len, &out) != 0) {
    buffer_free(&out);
    return NULL;
  }
  if(!out.data) {
    char *empty = malloc(1u);
    if(empty) empty[0] = '\0';
    return empty;
  }
  return out.data;
}

static char *dup_cstr(const char *s) {
  if(!s) s = "";
  size_t len = strlen(s);
  char *copy = malloc(len + 1u);
  if(!copy) return NULL;
  memcpy(copy, s, len + 1u);
  return copy;
}

static int tag_name_matches(const char *tag_start, size_t max_len, const char *name) {
  size_t nlen = strlen(name);
  if(max_len < nlen) return 0;
  if(!ci_starts_with(tag_start, max_len, name)) return 0;
  if(max_len == nlen) return 1;
  char next = tag_start[nlen];
  return isspace((unsigned char)next) || next == '>' || next == '/';
}

static int find_xml_tag(const char *xml,
                        size_t len,
                        const char *tag_name,
                        const char **out_open_start,
                        const char **out_inner_start,
                        const char **out_inner_end,
                        const char **out_after_close) {
  size_t pos = 0;
  while(pos < len) {
    if(xml[pos] == '<' && pos + 1u < len && xml[pos + 1u] != '/' && xml[pos + 1u] != '!' &&
       xml[pos + 1u] != '?') {
      if(tag_name_matches(xml + pos + 1u, len - (pos + 1u), tag_name)) {
        size_t gt = pos + 1u;
        int in_quote = 0;
        char qchar = 0;
        while(gt < len) {
          if(in_quote) {
            if(xml[gt] == qchar) in_quote = 0;
          } else {
            if(xml[gt] == '"' || xml[gt] == '\'') {
              in_quote = 1;
              qchar = xml[gt];
            } else if(xml[gt] == '>') {
              break;
            }
          }
          gt++;
        }
        if(gt >= len) return 0;
        if(gt > pos + 1u && xml[gt - 1u] == '/') {
          if(out_open_start) *out_open_start = xml + pos;
          if(out_inner_start) *out_inner_start = xml + gt;
          if(out_inner_end) *out_inner_end = xml + gt;
          if(out_after_close) *out_after_close = xml + gt + 1u;
          return 1;
        }
        const char *inner_start = xml + gt + 1u;
        size_t search = gt + 1u;
        int depth = 1;
        while(search < len) {
          if(xml[search] == '<' && search + 1u < len) {
            if(xml[search + 1u] == '/' && search + 2u < len) {
              if(tag_name_matches(xml + search + 2u, len - (search + 2u), tag_name)) {
                size_t close_gt = search + 2u;
                while(close_gt < len && xml[close_gt] != '>') close_gt++;
                if(close_gt < len) {
                  depth--;
                  if(depth == 0) {
                    if(out_open_start) *out_open_start = xml + pos;
                    if(out_inner_start) *out_inner_start = inner_start;
                    if(out_inner_end) *out_inner_end = xml + search;
                    if(out_after_close) *out_after_close = xml + close_gt + 1u;
                    return 1;
                  }
                  search = close_gt + 1u;
                  continue;
                }
              }
            } else if(xml[search + 1u] != '!' && xml[search + 1u] != '?' &&
                      tag_name_matches(xml + search + 1u, len - (search + 1u), tag_name)) {
              size_t sub_gt = search + 1u;
              int sub_q = 0;
              char sub_qc = 0;
              while(sub_gt < len) {
                if(sub_q) {
                  if(xml[sub_gt] == sub_qc) sub_q = 0;
                } else {
                  if(xml[sub_gt] == '"' || xml[sub_gt] == '\'') {
                    sub_q = 1;
                    sub_qc = xml[sub_gt];
                  } else if(xml[sub_gt] == '>') {
                    break;
                  }
                }
                sub_gt++;
              }
              if(sub_gt < len) {
                if(xml[sub_gt - 1u] != '/') depth++;
                search = sub_gt + 1u;
                continue;
              }
            }
          }
          search++;
        }
      }
    }
    pos++;
  }
  return 0;
}

static char *extract_xml_tag_clean(const char *xml, size_t len, const char *const *names, size_t ncount) {
  for(size_t i = 0; i < ncount; ++i) {
    const char *is = NULL;
    const char *ie = NULL;
    if(find_xml_tag(xml, len, names[i], NULL, &is, &ie, NULL) && ie > is) {
      return dup_span_clean(is, (size_t)(ie - is));
    }
  }
  return dup_cstr("");
}

static char *extract_xml_tag_raw(const char *xml, size_t len, const char *const *names, size_t ncount) {
  for(size_t i = 0; i < ncount; ++i) {
    const char *is = NULL;
    const char *ie = NULL;
    if(find_xml_tag(xml, len, names[i], NULL, &is, &ie, NULL) && ie > is) {
      return dup_span_raw_trimmed(is, (size_t)(ie - is));
    }
  }
  return dup_cstr("");
}

static char *extract_attr_value(const char *tag, size_t tag_len, const char *attr_name) {
  size_t alen = strlen(attr_name);
  size_t pos = 0;
  while(pos + alen < tag_len) {
    if(ci_starts_with(tag + pos, tag_len - pos, attr_name)) {
      if(pos == 0 || isspace((unsigned char)tag[pos - 1u])) {
        size_t eq = pos + alen;
        while(eq < tag_len && isspace((unsigned char)tag[eq])) eq++;
        if(eq < tag_len && tag[eq] == '=') {
          size_t val = eq + 1u;
          while(val < tag_len && isspace((unsigned char)tag[val])) val++;
          if(val < tag_len && (tag[val] == '"' || tag[val] == '\'')) {
            char q = tag[val++];
            size_t end = val;
            while(end < tag_len && tag[end] != q) end++;
            return dup_span_clean(tag + val, end - val);
          }
        }
      }
    }
    pos++;
  }
  return NULL;
}

static char *extract_feed_link(const char *xml, size_t len, const char *fallback) {
  const char *cursor = xml;
  size_t rem = len;
  char *first_href = NULL;
  while(rem > 0) {
    const char *open = NULL;
    const char *is = NULL;
    const char *ie = NULL;
    const char *after = NULL;
    if(!find_xml_tag(cursor, rem, "link", &open, &is, &ie, &after)) break;
    size_t open_len = (size_t)((is > open ? is : after) - open);
    char *href = extract_attr_value(open, open_len, "href");
    char *rel = extract_attr_value(open, open_len, "rel");
    if(href && href[0] != '\0') {
      if(!rel || ci_equals(rel, strlen(rel), "alternate")) {
        free(rel);
        free(first_href);
        return href;
      }
      if(!first_href) {
        first_href = href;
        href = NULL;
      }
    }
    free(href);
    free(rel);
    if(ie > is) {
      char *body = dup_span_clean(is, (size_t)(ie - is));
      if(body && body[0] != '\0') {
        free(first_href);
        return body;
      }
      free(body);
    }
    rem = (size_t)((xml + len) - after);
    cursor = after;
  }
  if(first_href) return first_href;
  return dup_cstr(fallback);
}

static char *extract_item_image_url(const char *xml, size_t len) {
  const char *img_tags[] = {"media:content", "media:thumbnail", "enclosure"};
  for(size_t t = 0; t < 3u; ++t) {
    const char *open = NULL;
    const char *is = NULL;
    const char *after = NULL;
    if(find_xml_tag(xml, len, img_tags[t], &open, &is, NULL, &after)) {
      size_t open_len = (size_t)((is > open ? is : after) - open);
      char *url = extract_attr_value(open, open_len, "url");
      if(url && url[0] != '\0') return url;
      free(url);
    }
  }
  const char *open = NULL;
  const char *is = NULL;
  const char *after = NULL;
  if(find_xml_tag(xml, len, "img", &open, &is, NULL, &after)) {
    size_t open_len = (size_t)((is > open ? is : after) - open);
    char *src = extract_attr_value(open, open_len, "src");
    if(src && src[0] != '\0') return src;
    free(src);
  }
  return dup_cstr("");
}

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

static int parse_month_abbrev(const char *s) {
  static const char *const k_months[12] = {
      "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  for(int m = 0; m < 12; ++m) {
    if(ci_starts_with(s, 3u, k_months[m])) return m + 1;
  }
  return 0;
}

uint64_t parse_timestamp_ms(const char *str) {
  if(!str) return 0u;
  while(*str && isspace((unsigned char)*str)) str++;
  if(!*str) return 0u;

  int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
  int tz_offset_sec = 0;

  /* Case 1: ISO-8601 / RFC 3339: YYYY-MM-DD[T ]HH:MM[:SS][Z|+HH:MM] */
  if(isdigit((unsigned char)str[0]) && isdigit((unsigned char)str[1]) &&
     isdigit((unsigned char)str[2]) && isdigit((unsigned char)str[3]) &&
     (str[4] == '-' || str[4] == '/')) {
    year = (str[0] - '0') * 1000 + (str[1] - '0') * 100 + (str[2] - '0') * 10 + (str[3] - '0');
    const char *p = str + 5;
    if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
      month = (p[0] - '0') * 10 + (p[1] - '0');
      p += 2;
      if(*p == '-' || *p == '/') p++;
      if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        day = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
      }
    }
    if(*p == 'T' || *p == 't' || *p == ' ') {
      p++;
      if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && p[2] == ':') {
        hour = (p[0] - '0') * 10 + (p[1] - '0');
        p += 3;
        if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
          min = (p[0] - '0') * 10 + (p[1] - '0');
          p += 2;
          if(*p == ':' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2])) {
            sec = (p[1] - '0') * 10 + (p[2] - '0');
            p += 3;
          }
          if(*p == '.') {
            p++;
            while(isdigit((unsigned char)*p)) p++;
          }
          if(*p == '+' || *p == '-') {
            int sign = (*p == '-') ? -1 : 1;
            p++;
            int tzh = 0, tzm = 0;
            if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
              tzh = (p[0] - '0') * 10 + (p[1] - '0');
              p += 2;
              if(*p == ':') p++;
              if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
                tzm = (p[0] - '0') * 10 + (p[1] - '0');
              }
            }
            tz_offset_sec = sign * (tzh * 3600 + tzm * 60);
          }
        }
      }
    }
  } else {
    /* Case 2: RFC 2822: [Day, ]DD Mon YYYY HH:MM[:SS] [GMT|+HHMM] */
    const char *p = str;
    const char *comma = strchr(p, ',');
    if(comma && (comma - p) <= 10) p = comma + 1;
    while(*p && isspace((unsigned char)*p)) p++;
    if(!isdigit((unsigned char)*p)) return 0u;
    while(isdigit((unsigned char)*p)) {
      day = day * 10 + (*p - '0');
      if(day > 99) return 0u;
      p++;
    }
    while(*p && (isspace((unsigned char)*p) || *p == '-')) p++;
    if(strlen(p) < 3u) return 0u;
    month = parse_month_abbrev(p);
    if(month == 0) return 0u;
    while(*p && isalpha((unsigned char)*p)) p++;
    while(*p && (isspace((unsigned char)*p) || *p == '-')) p++;
    while(isdigit((unsigned char)*p)) {
      year = year * 10 + (*p - '0');
      if(year > 99999) return 0u;
      p++;
    }
    if(year > 0 && year < 100) year += (year >= 70 ? 1900 : 2000);
    while(*p && isspace((unsigned char)*p)) p++;
    if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && p[2] == ':') {
      hour = (p[0] - '0') * 10 + (p[1] - '0');
      p += 3;
      if(isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        min = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if(*p == ':' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2])) {
          sec = (p[1] - '0') * 10 + (p[2] - '0');
          p += 3;
        }
      }
      while(*p && isspace((unsigned char)*p)) p++;
      if((*p == '+' || *p == '-') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
         isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4])) {
        int sign = (*p == '-') ? -1 : 1;
        int tzh = (p[1] - '0') * 10 + (p[2] - '0');
        int tzm = (p[3] - '0') * 10 + (p[4] - '0');
        tz_offset_sec = sign * (tzh * 3600 + tzm * 60);
      } else if(ci_starts_with(p, strlen(p), "EDT")) {
        tz_offset_sec = -4 * 3600;
      } else if(ci_starts_with(p, strlen(p), "EST")) {
        tz_offset_sec = -5 * 3600;
      } else if(ci_starts_with(p, strlen(p), "PDT")) {
        tz_offset_sec = -7 * 3600;
      } else if(ci_starts_with(p, strlen(p), "PST")) {
        tz_offset_sec = -8 * 3600;
      }
    }
  }

  if(year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
     hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
    return 0u;
  }
  int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
  int64_t total_sec = days * 86400LL + (int64_t)hour * 3600LL + (int64_t)min * 60LL +
                      (int64_t)sec - (int64_t)tz_offset_sec;
  if(total_sec <= 0) return 0u;
  return (uint64_t)total_sec * 1000ULL;
}

static void extract_item_categories(const char *xml, size_t len, ParsedFeedItem *item) {
  const char *cursor = xml;
  size_t rem = len;
  while(rem > 0) {
    const char *open = NULL;
    const char *is = NULL;
    const char *ie = NULL;
    const char *after = NULL;
    if(!find_xml_tag(cursor, rem, "category", &open, &is, &ie, &after)) break;
    size_t open_len = (size_t)((is > open ? is : after) - open);
    char *term = extract_attr_value(open, open_len, "term");
    if((!term || term[0] == '\0') && ie > is) {
      free(term);
      term = dup_span_clean(is, (size_t)(ie - is));
    }
    if(term && term[0] != '\0') {
      char **grown = realloc(item->categories, (item->category_count + 1u) * sizeof(char *));
      if(grown) {
        item->categories = grown;
        item->categories[item->category_count++] = term;
        term = NULL;
      }
    }
    free(term);
    rem = (size_t)((xml + len) - after);
    cursor = after;
  }
}

static char *extract_json_string_field(const char *json, size_t len, const char *key) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *found = ci_find(json, len, pattern);
  if(!found) return dup_cstr("");
  const char *p = found + strlen(pattern);
  const char *end = json + len;
  while(p < end && isspace((unsigned char)*p)) p++;
  if(p >= end || *p != ':') return dup_cstr("");
  p++;
  while(p < end && isspace((unsigned char)*p)) p++;
  if(p >= end || *p != '"') return dup_cstr("");
  p++;
  Buffer buf;
  buffer_init(&buf);
  while(p < end && *p != '"') {
    if(*p == '\\' && p + 1u < end) {
      p++;
      char c = *p;
      if(c == 'n') c = '\n';
      else if(c == 'r') c = '\r';
      else if(c == 't') c = '\t';
      buffer_append(&buf, &c, 1u);
    } else {
      buffer_append(&buf, p, 1u);
    }
    p++;
  }
  if(!buf.data) return dup_cstr("");
  return buf.data;
}

static void parsed_feed_item_free(ParsedFeedItem *it) {
  if(!it) return;
  free(it->guid);
  free(it->title);
  free(it->url);
  free(it->author);
  free(it->summary);
  free(it->content_html);
  free(it->published_at);
  free(it->image_url);
  for(size_t k = 0; k < it->category_count; ++k) {
    free(it->categories[k]);
  }
  free(it->categories);
  memset(it, 0, sizeof(*it));
}

static int parse_json_feed(const char *raw, size_t len, const char *base_url, ParsedFeed *out) {
  out->format = FEED_FORMAT_JSON;
  out->title = extract_json_string_field(raw, len, "title");
  if(out->title[0] == '\0') {
    free(out->title);
    out->title = dup_cstr("Untitled JSON Feed");
  }
  out->description = extract_json_string_field(raw, len, "description");
  out->site_url = extract_json_string_field(raw, len, "home_page_url");
  if(out->site_url[0] == '\0') {
    free(out->site_url);
    out->site_url = dup_cstr(base_url ? base_url : "");
  }
  out->language = extract_json_string_field(raw, len, "language");
  if(out->language[0] == '\0') {
    free(out->language);
    out->language = dup_cstr("en");
  }

  const char *items_key = ci_find(raw, len, "\"items\"");
  if(!items_key) return 0;
  const char *end = raw + len;
  const char *arr = (const char *)memchr(items_key, '[', (size_t)(end - items_key));
  if(!arr) return 0;
  const char *p = arr + 1u;

  while(p < end) {
    while(p < end && *p != '{' && *p != ']') p++;
    if(p >= end || *p == ']') break;
    const char *obj_start = p;
    int depth = 0;
    int in_str = 0;
    while(p < end) {
      if(in_str) {
        if(*p == '\\' && p + 1u < end) p++;
        else if(*p == '"') in_str = 0;
      } else {
        if(*p == '"') in_str = 1;
        else if(*p == '{') depth++;
        else if(*p == '}') {
          depth--;
          if(depth == 0) {
            p++;
            break;
          }
        }
      }
      p++;
    }
    size_t obj_len = (size_t)(p - obj_start);
    ParsedFeedItem item;
    memset(&item, 0, sizeof(item));
    item.title = extract_json_string_field(obj_start, obj_len, "title");
    if(item.title[0] == '\0') {
      free(item.title);
      item.title = dup_cstr("Untitled");
    }
    item.url = extract_json_string_field(obj_start, obj_len, "url");
    item.guid = extract_json_string_field(obj_start, obj_len, "id");
    if(item.guid[0] == '\0') {
      free(item.guid);
      item.guid = dup_cstr(item.url[0] ? item.url : item.title);
    }
    item.author = extract_json_string_field(obj_start, obj_len, "name");
    item.summary = extract_json_string_field(obj_start, obj_len, "summary");
    item.content_html = extract_json_string_field(obj_start, obj_len, "content_html");
    if(item.content_html[0] == '\0') {
      free(item.content_html);
      item.content_html = extract_json_string_field(obj_start, obj_len, "content_text");
    }
    if(item.summary[0] == '\0' && item.content_html[0] != '\0') {
      free(item.summary);
      item.summary = markup_clean_cstr(item.content_html);
    }
    item.published_at = extract_json_string_field(obj_start, obj_len, "date_published");
    item.image_url = extract_json_string_field(obj_start, obj_len, "image");

    ParsedFeedItem *grown = realloc(out->items, (out->item_count + 1u) * sizeof(ParsedFeedItem));
    if(!grown) {
      parsed_feed_item_free(&item);
      break;
    }
    out->items = grown;
    out->items[out->item_count++] = item;
  }
  return 0;
}

int feed_parse(const char *raw, size_t len, const char *base_url, ParsedFeed *out) {
  if(!raw || !out) return -1;
  memset(out, 0, sizeof(*out));
  while(len > 0 && isspace((unsigned char)*raw)) {
    raw++;
    len--;
  }
  if(len == 0) return -1;
  if(*raw == '{') {
    return parse_json_feed(raw, len, base_url, out);
  }

  int is_atom = ci_find(raw, len < 2048u ? len : 2048u, "<feed") != NULL;
  int is_rdf = ci_find(raw, len < 2048u ? len : 2048u, "<rdf:RDF") != NULL;
  out->format = is_atom ? FEED_FORMAT_ATOM : (is_rdf ? FEED_FORMAT_RSS1 : FEED_FORMAT_RSS2);

  const char *item_tag = is_atom ? "entry" : "item";
  const char *first_item = NULL;
  find_xml_tag(raw, len, item_tag, &first_item, NULL, NULL, NULL);
  size_t header_len = first_item ? (size_t)(first_item - raw) : len;

  const char *title_tags[] = {"title"};
  const char *desc_tags[] = {"description", "subtitle", "itunes:summary"};
  const char *lang_tags[] = {"language", "dc:language"};
  out->title = extract_xml_tag_clean(raw, header_len, title_tags, 1u);
  if(out->title[0] == '\0') {
    free(out->title);
    out->title = dup_cstr("Untitled Feed");
  }
  out->description = extract_xml_tag_clean(raw, header_len, desc_tags, 3u);
  out->site_url = extract_feed_link(raw, header_len, base_url ? base_url : "");
  out->language = extract_xml_tag_clean(raw, header_len, lang_tags, 2u);
  if(out->language[0] == '\0') {
    free(out->language);
    out->language = dup_cstr("en");
  }

  const char *cursor = raw;
  size_t rem = len;
  while(rem > 0) {
    const char *is = NULL;
    const char *ie = NULL;
    const char *after = NULL;
    if(!find_xml_tag(cursor, rem, item_tag, NULL, &is, &ie, &after)) break;
    size_t ilen = (size_t)(ie - is);

    ParsedFeedItem item;
    memset(&item, 0, sizeof(item));
    item.title = extract_xml_tag_clean(is, ilen, title_tags, 1u);
    if(item.title[0] == '\0') {
      free(item.title);
      item.title = dup_cstr("Untitled");
    }
    item.url = extract_feed_link(is, ilen, base_url ? base_url : "");
    const char *guid_tags[] = {"guid", "id"};
    item.guid = extract_xml_tag_clean(is, ilen, guid_tags, 2u);
    if(item.guid[0] == '\0') {
      free(item.guid);
      item.guid = dup_cstr(item.url[0] ? item.url : item.title);
    }
    const char *author_tags[] = {"dc:creator", "author", "name"};
    item.author = extract_xml_tag_clean(is, ilen, author_tags, 3u);
    const char *summary_tags[] = {"description", "summary"};
    item.summary = extract_xml_tag_clean(is, ilen, summary_tags, 2u);
    const char *content_tags[] = {"content:encoded", "content", "description", "summary"};
    item.content_html = extract_xml_tag_raw(is, ilen, content_tags, 4u);
    if(item.summary[0] == '\0' && item.content_html[0] != '\0') {
      free(item.summary);
      item.summary = markup_clean_cstr(item.content_html);
    }
    const char *date_tags[] = {"pubDate", "published", "updated", "dc:date"};
    item.published_at = extract_xml_tag_clean(is, ilen, date_tags, 4u);
    item.image_url = extract_item_image_url(is, ilen);
    extract_item_categories(is, ilen, &item);

    ParsedFeedItem *grown = realloc(out->items, (out->item_count + 1u) * sizeof(ParsedFeedItem));
    if(!grown) {
      parsed_feed_item_free(&item);
      break;
    }
    out->items = grown;
    out->items[out->item_count++] = item;

    rem = (size_t)((raw + len) - after);
    cursor = after;
  }

  return 0;
}

void feed_free(ParsedFeed *feed) {
  if(!feed) return;
  free(feed->title);
  free(feed->description);
  free(feed->site_url);
  free(feed->language);
  for(size_t i = 0; i < feed->item_count; ++i) {
    parsed_feed_item_free(&feed->items[i]);
  }
  free(feed->items);
  memset(feed, 0, sizeof(*feed));
}

const char *feed_format_name(FeedFormat format) {
  switch(format) {
    case FEED_FORMAT_RSS2: return "rss2";
    case FEED_FORMAT_ATOM: return "atom";
    case FEED_FORMAT_RSS1: return "rss1";
    case FEED_FORMAT_JSON: return "json";
    default: return "unknown";
  }
}

static char *extract_meta_tag(const char *html, size_t len, const char *key) {
  const char *cursor = html;
  size_t rem = len;
  while(rem > 0) {
    const char *meta = ci_find(cursor, rem, "<meta");
    if(!meta) break;
    size_t offset = (size_t)(meta - html);
    size_t gt = offset + 5u;
    while(gt < len && html[gt] != '>') gt++;
    if(gt >= len) break;
    size_t tag_len = gt - offset + 1u;
    char *prop = extract_attr_value(meta, tag_len, "property");
    char *name = extract_attr_value(meta, tag_len, "name");
    int match = (prop && ci_equals(prop, strlen(prop), key)) ||
                (name && ci_equals(name, strlen(name), key));
    free(prop);
    free(name);
    if(match) {
      char *content = extract_attr_value(meta, tag_len, "content");
      if(content) return content;
    }
    cursor = html + gt + 1u;
    rem = len - (gt + 1u);
  }
  return NULL;
}

static double score_html_candidate(const char *inner, size_t len, const char *tag_name) {
  Buffer text;
  buffer_init(&text);
  markup_strip_html(inner, len, &text);
  if(text.len < 60u) {
    buffer_free(&text);
    return -100.0;
  }

  double score = 0.0;
  if(ci_equals(tag_name, strlen(tag_name), "article")) score += 40.0;
  else if(ci_equals(tag_name, strlen(tag_name), "main")) score += 30.0;
  else if(ci_equals(tag_name, strlen(tag_name), "section")) score += 12.0;
  else score += 5.0;

  size_t p_count = 0;
  const char *p = inner;
  size_t rem = len;
  while(rem > 0) {
    const char *found = ci_find(p, rem, "<p");
    if(!found) break;
    p_count++;
    size_t adv = (size_t)(found - p) + 2u;
    p += adv;
    rem -= adv;
  }
  score += (double)p_count * 8.0;

  size_t commas = 0;
  for(size_t i = 0; i < text.len; ++i) {
    if(text.data[i] == ',' || text.data[i] == ';') commas++;
  }
  score += (double)commas * 2.0;
  score += (double)(text.len / 120u) < 45.0 ? (double)(text.len / 120u) : 45.0;

  buffer_free(&text);
  return score;
}

static void html_to_markdown(const char *html, size_t len, Buffer *md) {
  Buffer text_out;
  buffer_init(&text_out);
  const char *cursor = html;
  size_t rem = len;
  int found_blocks = 0;

  while(rem > 0) {
    const char *p = NULL;
    for(size_t i = 0; i + 2u < rem; ++i) {
      if(cursor[i] == '<') {
        if(tag_name_matches(cursor + i + 1u, rem - (i + 1u), "h1") ||
           tag_name_matches(cursor + i + 1u, rem - (i + 1u), "h2") ||
           tag_name_matches(cursor + i + 1u, rem - (i + 1u), "h3") ||
           tag_name_matches(cursor + i + 1u, rem - (i + 1u), "p") ||
           tag_name_matches(cursor + i + 1u, rem - (i + 1u), "li") ||
           tag_name_matches(cursor + i + 1u, rem - (i + 1u), "blockquote")) {
          p = cursor + i;
          break;
        }
      }
    }
    if(!p) break;
    char tname[16];
    size_t tn = 0;
    while(tn < 15u && (size_t)((p + 1u + tn) - cursor) < rem &&
          isalnum((unsigned char)p[1u + tn])) {
      tname[tn] = (char)tolower((unsigned char)p[1u + tn]);
      tn++;
    }
    tname[tn] = '\0';

    const char *is = NULL;
    const char *ie = NULL;
    const char *after = NULL;
    if(!find_xml_tag(p, (size_t)((cursor + rem) - p), tname, NULL, &is, &ie, &after)) {
      rem = (size_t)((cursor + rem) - (p + 1u));
      cursor = p + 1u;
      continue;
    }

    Buffer clean;
    buffer_init(&clean);
    markup_strip_html(is, (size_t)(ie - is), &clean);
    if(clean.len > 0) {
      found_blocks = 1;
      if(strcmp(tname, "h1") == 0) buffer_format(md, "# %s\n\n", clean.data);
      else if(strcmp(tname, "h2") == 0) buffer_format(md, "## %s\n\n", clean.data);
      else if(strcmp(tname, "h3") == 0) buffer_format(md, "### %s\n\n", clean.data);
      else if(strcmp(tname, "li") == 0) buffer_format(md, "- %s\n", clean.data);
      else if(strcmp(tname, "blockquote") == 0) buffer_format(md, "> %s\n\n", clean.data);
      else buffer_format(md, "%s\n\n", clean.data);
    }
    buffer_free(&clean);

    rem = (size_t)((cursor + rem) - after);
    cursor = after;
  }

  if(!found_blocks) {
    markup_strip_html(html, len, md);
  }
  while(md->len > 0 && isspace((unsigned char)md->data[md->len - 1u])) {
    md->len--;
    md->data[md->len] = '\0';
  }
  buffer_free(&text_out);
}

int readability_extract(const char *html, size_t len, const char *url, ExtractedContent *out) {
  (void)url;
  if(!html || !out) return -1;
  memset(out, 0, sizeof(*out));

  char *og_title = extract_meta_tag(html, len, "og:title");
  if(!og_title) og_title = extract_meta_tag(html, len, "twitter:title");
  if(!og_title) {
    const char *ttags[] = {"h1", "title"};
    og_title = extract_xml_tag_clean(html, len, ttags, 2u);
  }
  out->title = og_title ? og_title : dup_cstr("Untitled");

  char *byline = extract_meta_tag(html, len, "author");
  if(!byline) byline = extract_meta_tag(html, len, "article:author");
  out->byline = byline ? byline : dup_cstr("");

  char *excerpt = extract_meta_tag(html, len, "og:description");
  if(!excerpt) excerpt = extract_meta_tag(html, len, "description");
  out->excerpt = excerpt ? excerpt : dup_cstr("");

  char *site_name = extract_meta_tag(html, len, "og:site_name");
  out->site_name = site_name ? site_name : dup_cstr("");

  char *pub = extract_meta_tag(html, len, "article:published_time");
  out->published_time = pub ? pub : dup_cstr("");

  char *img = extract_meta_tag(html, len, "og:image");
  out->lead_image_url = img ? img : dup_cstr("");

  const char *candidates[] = {"article", "main", "section", "div"};
  const char *best_start = html;
  size_t best_len = len;
  double best_score = -1e9;

  for(size_t c = 0; c < 4u; ++c) {
    const char *cursor = html;
    size_t rem = len;
    while(rem > 0) {
      const char *is = NULL;
      const char *ie = NULL;
      const char *after = NULL;
      if(!find_xml_tag(cursor, rem, candidates[c], NULL, &is, &ie, &after)) break;
      size_t clen = (size_t)(ie - is);
      double sc = score_html_candidate(is, clen, candidates[c]);
      if(sc > best_score) {
        best_score = sc;
        best_start = is;
        best_len = clen;
      }
      rem = (size_t)((html + len) - after);
      cursor = after;
    }
  }

  Buffer text_buf;
  buffer_init(&text_buf);
  markup_strip_html(best_start, best_len, &text_buf);
  out->text_content = text_buf.data ? text_buf.data : dup_cstr("");

  Buffer md_buf;
  buffer_init(&md_buf);
  html_to_markdown(best_start, best_len, &md_buf);
  out->markdown = md_buf.data ? md_buf.data : dup_cstr(out->text_content);

  if(out->excerpt[0] == '\0' && out->text_content[0] != '\0') {
    free(out->excerpt);
    size_t elen = strlen(out->text_content);
    if(elen > 240u) {
      elen = 240u;
      while(elen > 0 && ((unsigned char)out->text_content[elen] & 0xC0u) == 0x80u) {
        elen--;
      }
    }
    out->excerpt = malloc(elen + 1u);
    if(out->excerpt) {
      memcpy(out->excerpt, out->text_content, elen);
      out->excerpt[elen] = '\0';
    } else {
      out->excerpt = dup_cstr("");
    }
  }

  uint32_t words = 0;
  int in_word = 0;
  for(const char *p = out->text_content; *p; ++p) {
    if(isspace((unsigned char)*p)) {
      in_word = 0;
    } else if(!in_word) {
      in_word = 1;
      words++;
    }
  }
  out->word_count = words;
  out->reading_time_minutes = words > 0 ? ((words + 219u) / 220u) : 1u;
  return 0;
}

void readability_free(ExtractedContent *content) {
  if(!content) return;
  free(content->title);
  free(content->byline);
  free(content->excerpt);
  free(content->site_name);
  free(content->published_time);
  free(content->lead_image_url);
  free(content->text_content);
  free(content->markdown);
  memset(content, 0, sizeof(*content));
}
