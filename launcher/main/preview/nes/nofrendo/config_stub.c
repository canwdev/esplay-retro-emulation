#include <stdbool.h>
#include <stddef.h>
#include "nofconfig.h"

static bool config_open(void) { return false; }
static void config_close(void) {}
static int config_read_int(const char *g, const char *k, int d) {
   (void)g; (void)k; return d;
}
static const char *config_read_string(const char *g, const char *k, const char *d) {
   (void)g; (void)k; return d;
}
static void config_write_int(const char *g, const char *k, int v) {
   (void)g; (void)k; (void)v;
}
static void config_write_string(const char *g, const char *k, const char *v) {
   (void)g; (void)k; (void)v;
}

config_t config = {
   .open        = config_open,
   .close       = config_close,
   .read_int    = config_read_int,
   .read_string = config_read_string,
   .write_int   = config_write_int,
   .write_string= config_write_string,
   .filename    = NULL,
};
