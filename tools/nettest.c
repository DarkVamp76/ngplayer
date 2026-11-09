#include "net.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    printf("curl: %d\n", net_have_curl());
    for (int i = 1; i < argc; i++) {
        int id = ma_parse_id(argv[i]);
        printf("%s -> id %d\n", argv[i], id);
        if (id) { char p[1100]; int r = ma_download(id, p, sizeof p); printf("download: %d %s\n", r, r ? "" : p); }
    }
    return 0;
}
