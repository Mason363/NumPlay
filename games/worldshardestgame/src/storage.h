#ifndef WHG_STORAGE_H
#define WHG_STORAGE_H
#define WHG_BACKUP_PREFIX "# The World's Hardest Game save\n# WHG1:"
static bool whg_storage_load(WhgProfile *p){
 uint32_t size=0;const uint8_t *b=ef_read("whg.sav",&size);if(whg_decode(p,b,size))return true;
 b=ef_read("whg_save.py",&size);unsigned prefix=sizeof(WHG_BACKUP_PREFIX)-1;
 if(!b||size!=prefix+WHG_SAVE_BYTES*2+1||memcmp(b,WHG_BACKUP_PREFIX,prefix)||b[size-1]!='\n')return false;
 uint8_t raw[WHG_SAVE_BYTES];for(unsigned i=0;i<WHG_SAVE_BYTES;i++){unsigned v=0;for(unsigned k=0;k<2;k++){unsigned c=b[prefix+i*2+k];if(c>='0'&&c<='9')c-='0';else if(c>='a'&&c<='f')c=c-'a'+10;else return false;v=v*16+c;}raw[i]=(uint8_t)v;}return whg_decode(p,raw,sizeof raw);
}
static bool whg_storage_save(const WhgProfile *p){
 uint8_t raw[WHG_SAVE_BYTES];whg_encode(p,raw);if(!ef_write("whg.sav",raw,sizeof raw))return false;
 static const char hex[]="0123456789abcdef";char backup[sizeof(WHG_BACKUP_PREFIX)-1+WHG_SAVE_BYTES*2+1];unsigned at=sizeof(WHG_BACKUP_PREFIX)-1;memcpy(backup,WHG_BACKUP_PREFIX,at);
 for(unsigned i=0;i<sizeof raw;i++){backup[at++]=hex[raw[i]>>4];backup[at++]=hex[raw[i]&15];}backup[at++]='\n';return ef_write("whg_save.py",backup,at);
}
#endif
