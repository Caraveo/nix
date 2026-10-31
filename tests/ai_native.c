/* The same runtime compiled on the host for numerical and tokenizer checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef unsigned char u8;typedef unsigned int u32;
#include "../kernel/ai/runtime.h"
int main(int argc,char **argv){
 if(argc<3)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc((size_t)n);if(fread(data,1,(size_t)n,f)!=(size_t)n)return 2;fclose(f);
 if(!ai_init(data,(u32)n)){fprintf(stderr,"invalid model\n");return 3;}
 if(!strcmp(argv[2],"dot-check")){
  signed char a[1536],b[1536];unsigned seed=42;
  for(int trial=0;trial<1000;trial++){
   int n=16*((trial%96)+1),expected=0;
   for(int j=0;j<n;j++){seed=seed*1664525U+1013904223U;a[j]=(signed char)(seed>>24);seed=seed*1664525U+1013904223U;b[j]=(signed char)(seed>>24);expected+=(int)a[j]*(int)b[j];}
   if(ai_dot(a,b,(u32)n)!=expected)return 6;
  }
  puts("PASS: 1000 SSE2 integer dot products equal independent scalar sums");return 0;
 }
 int tokens[AI_CONTEXT];
 if(!strcmp(argv[2],"tokenize")){
  if(argc<4)return 2;int count=ai_tokenize(argv[3],tokens,AI_CONTEXT);
  printf("[");for(int i=0;i<count;i++)printf("%s%d",i?",":"",tokens[i]);puts("]");return count<0?4:0;
 }
 int count=ai_prompt(argv[2],tokens,AI_CONTEXT-48);if(count<0)return 4;
 clock_t start=clock();
 for(int i=0;i<count;i++)if(!ai_forward(tokens[i],i,i==count-1))return 5;
 if(argc>3&&!strcmp(argv[3],"logits")){FILE *out=fopen("build/ai-logits.bin","wb");fwrite(ai.logits,4,AI_VOCAB,out);fclose(out);}
 for(int pos=count;pos<count+40&&pos<AI_CONTEXT;pos++){
  int token=ai_argmax();if(token<=2)break;
  fwrite(ai.vocabulary+ai.offsets[token],1,ai.offsets[token+1]-ai.offsets[token],stdout);fflush(stdout);
  if(!ai_forward(token,pos,1))return 5;
 }
 printf("\n");fprintf(stderr,"%.2fs CPU / %d prompt tokens\n",(double)(clock()-start)/CLOCKS_PER_SEC,count);free(data);return 0;
}
