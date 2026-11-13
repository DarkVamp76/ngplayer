/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Netzwerk-Hilfen: Download ueber den Hilfsprozess curl, Cache-Verzeichnis, The-Mod-Archive-Download. */
#pragma once
#include <stddef.h>

/* Cache-Verzeichnis ($XDG_CACHE_HOME/ngplayer bzw. ~/.cache/ngplayer), wird angelegt. 0 = ok */
int net_cache_dir(char *out, size_t cap);
/* Laedt url nach dest (erst in dest.part, dann atomar umbenannt). 0 = ok, sonst Fehlercode (curl-Exitcode, -1 = Start/Rename) */
int net_get(const char *url, const char *dest, int timeout_s);
/* Ist curl verfuegbar? */
int net_have_curl(void);

/* The Mod Archive: Modul per ID laden (Cache: <cache>/modarchive/<id>.mod). Liefert 0 und den Pfad in out. */
int ma_download(int id, char *out, size_t cap);
/* Modul-ID aus Eingabe: reine Zahl oder Link mit "moduleid=", "?request=view_by_moduleid&query=" bzw. ".../module/..._<id>". 0 = nicht erkannt */
int ma_parse_id(const char *s);
/* The Mod Archive: ID of a random module (asks the "random" page, can be slow). 0 = failed. Not available in the web build (done by ma.php there). */
int ma_random_id(void);
