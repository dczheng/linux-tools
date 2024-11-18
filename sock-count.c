#include "bpf.h"
#include "config.h"

int
main(int argc, char **argv) {
    char *name = DEFAULT_IFACE;
    int sock = -1, map = -1, prog = -1, i, j, ret = 0;
    uint32_t key;
    uint64_t value;

    if (argc > 1) name = argv[1];
    LOG("interface: %s\n", name);

    TRY(!(ret = bpf_map_create(&map, BPF_MAP_TYPE_ARRAY,
        sizeof(key), sizeof(value), IPPROTO_MAX)),
        goto err);

    struct bpf_insn insns[] = {
        bpf_mov64i(bpf_r2, ETH_HLEN + offsetof(struct iphdr, protocol)),
        bpf_mov64(bpf_r3, bpf_r10),
        bpf_add64i(bpf_r3, -4),
        bpf_mst32i(bpf_r3, 0, 0),
        bpf_mov64i(bpf_r4, 1),
        bpf_call(BPF_FUNC_skb_load_bytes),
        bpf_jne64i(bpf_r0, 0, 8),

        bpf_mov64(bpf_r2, bpf_r10),
        bpf_add64i(bpf_r2, -4),
        bpf_load_fd(bpf_r1, map),
        bpf_call(BPF_FUNC_map_lookup_elem),
        bpf_jeq64i(bpf_r0, 0, 2),

        bpf_mov64i(bpf_r1, 1),
        bpf_atomic_add64(bpf_r0, 0, bpf_r1),

        bpf_return(0),
    };

    TRY(!(ret = bpf_prog_load(&prog, BPF_PROG_TYPE_SOCKET_FILTER, insns,
        LEN(insns), "MIT", 2048)), goto err);

    TRY(!(ret = if_attach(&sock, name, prog)), goto err);

    for (i = 0; i < 10; i++) {
        for (j = 0; j < IPPROTO_MAX; j++) {
            switch(j){
#define _case(t) \
        case IPPROTO_##t: \
            key = j; \
            TRY(!(ret = bpf_map_lookup(map, &key, &value)), goto err); \
            LOG("%4s: %3lu ", #t, value); \
            break;

            _case(TCP);
            _case(UDP);
            _case(ICMP);
#undef _case
            }
        }
        LOG("\n");
        sleep(1);
    }

err:
    if (sock > 0) close(sock);
    if (map > 0) close(map);
    if (prog > 0) close(prog);
    if (ret) LOGERR("%s\n", strerror(ret));
    return ret;
}
