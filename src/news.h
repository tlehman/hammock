#ifndef NEWS_H
#define NEWS_H

/* Register the view-news command. The NEWS.md file is embedded into
 * the binary at build time via xxd (see Makefile). */
void news_init(void);

#endif
