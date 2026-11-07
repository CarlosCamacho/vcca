#include "partition_fat.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static bool supported_type(uint8_t type) {
    return type==0x01u || type==0x04u || type==0x06u || type==0x0Eu || type==0x0Bu || type==0x0Cu;
}

bool emes_mbr_enumerate(const uint8_t sector[512], uint32_t ide_sectors,
                        emes_mbr_partition_t out[4], size_t *count) {
    if (sector==NULL || out==NULL || count==NULL || sector[510]!=0x55u || sector[511]!=0xAAu) return false;
    size_t used=0u;
    for (uint8_t i=0u;i<4u;i++) {
        const uint8_t *e=sector+446u+(uint16_t)i*16u;
        uint8_t status=e[0], type=e[4]; uint32_t first=le32(e+8), sectors=le32(e+12);
        if (type==0u || sectors==0u) continue;
        if ((status!=0u && status!=0x80u) || first==0u || first>ide_sectors || sectors>ide_sectors-first) return false;
        for (size_t j=0u;j<used;j++) {
            uint32_t a0=out[j].first_lba, a1=a0+out[j].sectors;
            uint32_t b0=first, b1=first+sectors;
            if (a0<b1 && b0<a1) return false;
        }
        out[used].index=i; out[used].bootable=status==0x80u; out[used].type=type;
        out[used].first_lba=first; out[used].sectors=sectors; used++;
    }
    *count=used; return true;
}

bool emes_mbr_create_primary(uint8_t sector[512], uint32_t ide_sectors, uint8_t index,
                             uint32_t first_lba, uint32_t sectors, uint8_t type, bool bootable) {
    if (sector==NULL || index>=4u || !supported_type(type) || first_lba==0u || sectors<64u ||
        first_lba>ide_sectors || sectors>ide_sectors-first_lba) return false;
    uint8_t candidate[512]; memcpy(candidate,sector,512u); candidate[510]=0x55u; candidate[511]=0xAAu;
    uint8_t *e=candidate+446u+(uint16_t)index*16u; memset(e,0,16u);
    e[0]=bootable?0x80u:0u; e[1]=1u; e[2]=1u; e[4]=type; e[5]=0xFEu; e[6]=0xFFu; e[7]=0xFFu;
    put32(e+8,first_lba); put32(e+12,sectors);
    emes_mbr_partition_t parts[4]; size_t count=0u;
    if (!emes_mbr_enumerate(candidate,ide_sectors,parts,&count)) return false;
    memcpy(sector,candidate,512u); return true;
}

bool emes_fat_probe(const uint8_t boot[512], uint32_t partition_sectors, emes_fat_probe_t *out) {
    if (boot==NULL || out==NULL || boot[510]!=0x55u || boot[511]!=0xAAu || le16(boot+11)!=512u) return false;
    uint8_t spc=boot[13], fats=boot[16]; uint16_t reserved=le16(boot+14), roots=le16(boot+17);
    uint32_t total=le16(boot+19); if (total==0u) total=le32(boot+32);
    uint32_t spf=le16(boot+22); if (spf==0u) spf=le32(boot+36);
    if (spc==0u || reserved==0u || fats==0u || spf==0u || total==0u || total>partition_sectors) return false;
    uint32_t root_sectors=((uint32_t)roots*32u+511u)/512u;
    uint32_t overhead=(uint32_t)reserved+(uint32_t)fats*spf+root_sectors;
    if (overhead>=total) return false;
    uint32_t clusters=(total-overhead)/spc; emes_fat_kind_t kind;
    if (roots==0u) kind=EMES_FAT32; else if (clusters<4085u) kind=EMES_FAT12; else if (clusters<=65524u) kind=EMES_FAT16; else return false;
    out->kind=kind; out->read_only=kind==EMES_FAT32; out->sectors_per_cluster=spc;
    out->reserved_sectors=reserved; out->fat_count=fats; out->root_entries=roots;
    out->sectors_per_fat=spf; out->total_sectors=total; out->cluster_count=clusters; return true;
}

bool emes_fat_format_boot(uint8_t boot[512], uint32_t total, emes_fat_kind_t kind,
                          uint32_t hidden, const char *label, emes_fat_format_layout_t *layout) {
    if (boot==NULL || layout==NULL || (kind!=EMES_FAT12 && kind!=EMES_FAT16) || total<256u) return false;
    uint16_t roots=kind==EMES_FAT12?224u:512u; uint16_t root_sectors=(uint16_t)(((uint32_t)roots*32u+511u)/512u);
    uint16_t spf=1u; uint32_t clusters=0u;
    for (uint8_t pass=0u;pass<16u;pass++) {
        uint32_t overhead=1u+2u*(uint32_t)spf+(uint32_t)root_sectors; if (overhead>=total) return false;
        clusters=total-overhead;
        uint32_t fat_bytes=kind==EMES_FAT12?((clusters+2u)*3u+1u)/2u:(clusters+2u)*2u;
        uint16_t next=(uint16_t)((fat_bytes+511u)/512u); if (next==spf) break; spf=next;
    }
    if ((kind==EMES_FAT12 && clusters>=4085u) || (kind==EMES_FAT16 && (clusters<4085u || clusters>65524u))) return false;
    memset(boot,0,512u); boot[0]=0xEBu; boot[1]=0x3Cu; boot[2]=0x90u; memcpy(boot+3,"EMESDOS9",8u);
    put16(boot+11,512u); boot[13]=1u; put16(boot+14,1u); boot[16]=2u; put16(boot+17,roots);
    if (total<65536u) put16(boot+19,(uint16_t)total); else put32(boot+32,total);
    boot[21]=0xF8u; put16(boot+22,spf); put16(boot+24,63u); put16(boot+26,16u); put32(boot+28,hidden);
    boot[36]=0x80u; boot[38]=0x29u; put32(boot+39,0xE9240010u);
    memset(boot+43,' ',11u); if (label!=NULL) { for (uint8_t i=0u;i<11u && label[i]!='\0';i++) boot[43+i]=(uint8_t)label[i]; }
    memcpy(boot+54,kind==EMES_FAT12?"FAT12   ":"FAT16   ",8u); boot[510]=0x55u; boot[511]=0xAAu;
    layout->root_entries=roots; layout->root_sectors=root_sectors; layout->sectors_per_fat=spf; layout->sectors_per_cluster=1u;
    return true;
}
