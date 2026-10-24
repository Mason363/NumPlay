#ifndef FOOTBALL_STORAGE_H
#define FOOTBALL_STORAGE_H
/* Epsilon preserves Python scripts when external applications are reinstalled.
 * This script is inert hex data in comments; no Python code is executed. */
#define FC_BACKUP_PREFIX "# Football FC profile backup\n# FFCP1:"
static bool fc_storage_load(FCProfile*p){
 uint32_t size=0;const uint8_t*b=ef_read("footballfc.sav",&size);if(fc_decode(p,b,size))return true;
 b=ef_read("footballfc_save.py",&size);const unsigned prefix=sizeof(FC_BACKUP_PREFIX)-1;
 if(!b||size!=prefix+FC_SAVE_BYTES*2+1||memcmp(b,FC_BACKUP_PREFIX,prefix)||b[size-1]!='\n')return false;
 uint8_t raw[FC_SAVE_BYTES];for(unsigned i=0;i<FC_SAVE_BYTES;i++){unsigned v=0;for(unsigned k=0;k<2;k++){unsigned c=b[prefix+i*2+k];if(c>='0'&&c<='9')c-='0';else if(c>='a'&&c<='f')c=c-'a'+10;else return false;v=v*16+c;}raw[i]=(uint8_t)v;}
 return fc_decode(p,raw,sizeof raw);
}
static bool fc_storage_save(const FCProfile*p){
 uint8_t raw[FC_SAVE_BYTES];fc_encode(p,raw);if(!ef_write("footballfc.sav",raw,sizeof raw))return false;
 static const char hex[]="0123456789abcdef";char copy[sizeof(FC_BACKUP_PREFIX)-1+FC_SAVE_BYTES*2+1];unsigned at=sizeof(FC_BACKUP_PREFIX)-1;
 memcpy(copy,FC_BACKUP_PREFIX,at);for(unsigned i=0;i<sizeof raw;i++){copy[at++]=hex[raw[i]>>4];copy[at++]=hex[raw[i]&15];}copy[at++]='\n';return ef_write("footballfc_save.py",copy,at);
}
#endif
