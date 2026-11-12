#!/usr/bin/env python3
"""纯 C 配置导入负例与截断/数值边界 sanitizer 回归。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_store.h"
#include "json_miniparse.h"
int main(void) {
 const char *invalid[] = {
  "{\"schema_ver\":14,\"power\":2 60,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":- 5,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":01,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":260oops,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":1.5,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":1e2,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":2147483648,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":-2147483649,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":null,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"power\":260,\"power\":300,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"led_silent\":256,\"bridge_devices\":[]}",
  "{\"schema_ver\":14,\"bridge_devices\":[]} trailing",
  "{\"schema_ver\":14,\"bridge_devices\":[],}",
  "{\"schema_ver\":14,\"bridge_devices\":[],\"unknown\":{\"x\":1,\"x\":2}}",
  "{\"schema_ver\":14,\"bridge_devices\":[],\"unknown\":\"bad\\q\"}",
  "{\"schema_ver\":14,\"bridge_devices\":[],\"unknown\":[1,]}",
  "{\"schema_ver\":14,\"bridge_devices\":[{\"addr_type\":256,\"address\":\"0123456789ab\",\"name\":\"A\"}]}",
  "{\"schema_ver\":14,\"bridge_devices\":[],\"powe\\u0072\":300}",
 };
 config_data_t c, before;
 config_store_defaults(&c); c.power = 377; before = c;
 for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
   if(config_store_deserialize(invalid[i],&c)) {fprintf(stderr,"accepted invalid %u\n",i); return 1;}
   assert(memcmp(&c,&before,sizeof(c))==0);
 }
 assert(config_store_deserialize(" { \"schema_ver\" : 14, \"power\" : 345, \"device_name\" : \"Unit Test\", \"bridge_devices\" : [] } ",&c));
 assert(c.power==345 && strcmp(c.device_name,"Unit Test")==0);
 assert(config_store_deserialize("{\"schema_ver\":14,\"unknown\":{\"power\":600},\"bridge_devices\":[]}",&c));
 assert(c.power==DEFAULT_POWER);
 assert(config_store_deserialize("{\"schema_ver\":14,\"power\":2147483647,\"bridge_devices\":[]}",&c));
 assert(c.power==POWER_MAX);
 assert(config_store_deserialize("{\"schema_ver\":14,\"power\":-2147483648,\"bridge_devices\":[]}",&c));
 assert(c.power==POWER_MIN);
 char text[CONFIG_JSON_MAX_LEN]; config_store_defaults(&c); assert(config_store_serialize(&c,text,sizeof(text)));
 char normalized[CONFIG_JSON_MAX_LEN];
 int untouched=77;
 assert(!json_get_int_checked("{\"x\":01}","x",&untouched) && untouched==77);
 char output[4]="old";
 assert(!json_get_str("{\"x\":\"long\"}","x",output,sizeof(output)) && strcmp(output,"old")==0);
 assert(!json_get_str(NULL,"x",output,sizeof(output)));
 char nested[256]="0";
 for(unsigned depth=0;depth<JSON_IMPORT_MAX_DEPTH;depth++) {
  char next[256]; snprintf(next,sizeof(next),"[%.240s]",nested); strcpy(nested,next);
 }
 char deep[512]; snprintf(deep,sizeof(deep),"{\"x\":%s}",nested);
 assert(!json_normalize_object(deep,normalized,sizeof(normalized)));
 nested[strlen(nested)-1]=0;
 snprintf(deep,sizeof(deep),"{\"x\":%s}",nested+1);
 assert(json_normalize_object(deep,normalized,sizeof(normalized)));
 memset(deep,' ',sizeof(deep)); deep[sizeof(deep)-1]=0;
 assert(!json_normalize_object(deep,normalized,128));
 size_t n=strlen(text); for(size_t i=0;i<n;i++) {char cut[CONFIG_JSON_MAX_LEN]; memcpy(cut,text,i); cut[i]=0; assert(!config_store_deserialize(cut,&c));}
 unsigned seed=12345; const char alphabet[]="{}[],:0123456789-+ abcdef\\\"\\\\";
 for(unsigned round=0;round<2000;round++) {
  char fuzz[80]; unsigned length=round%79;
  for(unsigned j=0;j<length;j++){seed=seed*1664525u+1013904223u;fuzz[j]=alphabet[seed%(sizeof(alphabet)-1)];}
  fuzz[length]=0; before=c; if(!config_store_deserialize(fuzz,&c)) assert(memcmp(&c,&before,sizeof(c))==0);
 }
 puts("strict JSON tokens/duplicates/scope/whitespace/int bounds/truncation/atomic output PASS");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='bikepower-json-') as temp:
        folder = Path(temp)
        source = folder / 'test.c'
        source.write_text(SOURCE, encoding='utf-8')
        binary = folder / 'test'
        domain = ROOT / 'components/bikepower_domain'
        files = [p for p in domain.glob('*.c')]
        subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(domain/'include'), str(source),
                        *map(str,files), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    main()
