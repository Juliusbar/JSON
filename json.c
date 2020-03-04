/*
 * JSON C Library
 * 
 * Copyright (c) 2018-2019 Julius Barzdziukas <julius.barzdziukas@gmail.com>
 * Copyright (c) 2019 CUJO LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "json.h"

//File block size during file read
#define buff_n 10000
//String buffer size
#define str_value_n 1000

#define control_characters 0
#define brackets_part_of_string 0
#define strict_string_quotes 0
#define C_escape_characters 1

#define ALLOC(p,n) ((p)=malloc(sizeof(*(p))*(n)))

struct buffer{
	size_t n;
	uint8_t s[str_value_n];
	struct buffer *next;
};

struct sequence{
	uint8_t type; // 0 no data, 1 object, 2 value
	uint8_t visited,array;
	union{
		struct json_object *object;
		struct json_value *value;
	};
	struct sequence *prev,*next;
};

struct buffer *buffer_new(){
	struct buffer *B;
	ALLOC(B,1);
	if(B){
		B->next=0;
		B->n=0;
	}
	return B;
}

int buff_add(struct buffer **A,uint8_t c){
	if((*A)->n>=str_value_n){
		(*A)->next=buffer_new();
		if(!(*A)->next){
			return 1;
		}
		*A=(*A)->next;
	}
	(*A)->s[((*A)->n)++]=c;
	return 0;
}

int buff_add_array(struct buffer **A,uint8_t *B,size_t n){
	size_t i;
	for(i=0;i<n;i++){
		if((*A)->n>=str_value_n){
			(*A)->next=buffer_new();
			if(!(*A)->next){
				return 1;
			}
			*A=(*A)->next;
		}
		(*A)->s[((*A)->n)++]=B[i];
	}
	return 0;
}

#define buff_add_array_s(A,B,n) buff_add_array((A),(uint8_t*)(B),(n))

int buff_add_buff(struct buffer **A,struct buffer *B){
	size_t i;
	while(B){
		for(i=0;i<B->n;i++){
			if((*A)->n>=str_value_n){
				(*A)->next=buffer_new();
				if(!(*A)->next){
					return 1;
				}
				*A=(*A)->next;
			}
			(*A)->s[((*A)->n)++]=B->s[i];
		}
		B=B->next;
	}
	return 0;
}

size_t utf32_to_utf8(uint32_t C,uint8_t *A,size_t n){
	size_t n1=0;
	if(C<=0x7F){
		if(n<=1) return 0;
		A[n1++]=C;
	}else if(C<=0x7FF){
		if(n<=2) return 0;
		A[n1++]=(C>>6)|0xC0;
		A[n1++]=(C&0x3F)|0x80;
	}else if(C<=0xFFFF){
		if(n<=3) return 0;
		A[n1++]=(C>>12)|0xE0;
		A[n1++]=((C>>6)&0x3F)|0x80;
		A[n1++]=(C&0x3F)|0x80;
	}else if(C<=0x1FFFFF){
		if(n<=4) return 0;
		A[n1++]=(C>>18)|0xF0;
		A[n1++]=((C>>12)&0x3F)|0x80;
		A[n1++]=((C>>6)&0x3F)|0x80;
		A[n1++]=(C&0x3F)|0x80;
	}else if(C<=0x3FFFFFF){
		if(n<=5) return 0;
		A[n1++]=(C>>24)|0xF8;
		A[n1++]=((C>>18)&0x3F)|0x80;
		A[n1++]=((C>>12)&0x3F)|0x80;
		A[n1++]=((C>>6)&0x3F)|0x80;
		A[n1++]=(C&0x3F)|0x80;
	}else if(C<=0x7FFFFFFF){
		if(n<=6) return 0;
		A[n1++]=(C>>28)|0xFC;
		A[n1++]=((C>>24)&0x3F)|0x80;
		A[n1++]=((C>>18)&0x3F)|0x80;
		A[n1++]=((C>>12)&0x3F)|0x80;
		A[n1++]=((C>>6)&0x3F)|0x80;
		A[n1++]=(C&0x3F)|0x80;
	}else{ //Error Unicode char >0x7FFFFFFF
		return 0;
	}
	return n1;
}

size_t utf8_to_utf32(uint32_t C,uint8_t *A,size_t n){
	size_t n1=0;
	if((A[n1]&0x80)==0){
		if(n<=1) return 0;
		C=A[n1++];
	}else if((A[n1]&0xE0)==0xC0){
		if(n<=2) return 0;
		for(n1++;n1<2;n1++)
			if((A[n1]&0xC0)!=0x80) return 0;
		n1=0;
		C=((uint32_t)(A[n1++]&0x1F))<<6;
		C|=A[n1++]&0x3F;
	}else if((A[n1]&0xF0)==0xE0){
		if(n<=3) return 0;
		for(n1++;n1<3;n1++)
			if((A[n1]&0xC0)!=0x80) return 0;
		n1=0;
		C=((uint32_t)(A[n1++]&0x0F))<<12;
		C|=((uint32_t)(A[n1++]&0x3F))<<6;
		C|=A[n1++]&0x3F;
	}else if((A[n1]&0xF8)==0xF0){
		if(n<=4) return 0;
		for(n1++;n1<4;n1++)
			if((A[n1]&0xC0)!=0x80) return 0;
		n1=0;
		C=((uint32_t)(A[n1++]&0x07))<<18;
		C|=((uint32_t)(A[n1++]&0x3F))<<12;
		C|=((uint32_t)(A[n1++]&0x3F))<<6;
		C|=A[n1++]&0x3F;
	}else if((A[n1]&0xFC)==0xF8){
		if(n<=5) return 0;
		for(n1++;n1<5;n1++)
			if((A[n1]&0xC0)!=0x80) return 0;
		n1=0;
		C=((uint32_t)(A[n1++]&0x03))<<24;
		C|=((uint32_t)(A[n1++]&0x3F))<<18;
		C|=((uint32_t)(A[n1++]&0x3F))<<12;
		C|=((uint32_t)(A[n1++]&0x3F))<<6;
		C|=A[n1++]&0x3F;
	}else if((A[n1]&0xFE)==0xFC){
		if(n<=6) return 0;
		for(n1++;n1<5;n1++)
			if((A[n1]&0xC0)!=0x80) return 0;
		n1=0;
		C=((uint32_t)(A[n1++]&0x01))<<28;
		C|=((uint32_t)(A[n1++]&0x3F))<<24;
		C|=((uint32_t)(A[n1++]&0x3F))<<18;
		C|=((uint32_t)(A[n1++]&0x3F))<<12;
		C|=((uint32_t)(A[n1++]&0x3F))<<6;
		C|=A[n1++]&0x3F;
	}else{ //Error Non Unicode char
		return 0;
	}
	return n1;
}

int buff_delete(struct buffer *A,struct buffer **B){
	struct buffer *P;
	*B=A;
	P=A->next;
	A->next=0;
	A->n=0;
	while(P){
		A=P;
		P=P->next;
		free(A);
	}
	return 0;
}

size_t buff_length(struct buffer *P,size_t w){
	size_t n=0;
	while(P){
		n+=P->n;
		P=P->next;
	}
	if(n>w){
		n-=w;
		return n;
	}
	return 0;
}

char * buff_toarray(struct buffer *P,size_t *n,size_t w){
	size_t i,j;
	char *str;
	*n=buff_length(P,w);
	ALLOC(str,*n+1);
	if(!str) return 0;
	i=0;
	while(P){
		for(j=0;(j<P->n)&&(i<(*n));i++,j++) str[i]=P->s[j];
		P=P->next;
	}
	str[*n]=0;
	return str;
}

struct json_object * object_new(){
	struct json_object *O;
	ALLOC(O,1);
	if(O){
		O->next=0;
		O->value=0;
		O->name=0;
		O->n=0;
	}
	return O;
}

int object_str(struct json_object *O,struct buffer *S, size_t w){
	if((O->name=buff_toarray(S,&(O->n),w))) return 0;
	else return 1;
}

struct json_value * value_new(){
	struct json_value *V;
	ALLOC(V,1);
	if(V){
		V->type=0;
		V->next=0;
		V->value_string=0;
		V->n=0;
	}
	return V;
}

int value_str(struct json_value *V,struct buffer *S,size_t w){
	V->type=3;
	if((V->value_string=buff_toarray(S,&(V->n),w))) return 0;
	else return 1;
}

struct sequence * seq_new(){
	struct sequence *seq;
	ALLOC(seq,1);
	if(seq){
		seq->next=0;
		seq->visited=0;
		seq->array=0;
	}
	return seq;
}

int seq_next_new(struct sequence **seq,uint8_t type,uint8_t next){
	struct sequence *S;
	struct json_object *O;
	struct json_value *V;
	S=seq_new();
	if(!S) return 1;
	if(*seq){
		(*seq)->next=S;
		S->prev=*seq;
	}else{
		S->prev=0;
	}
	S->type=type;
	if(type==1){ //Object
		O=object_new();
		if(!O) return 2;
		S->object=O;
		if(next){
			if(*seq){
				if((*seq)->type==1) (*seq)->object->next=O;
				else return 3; //Value next cannot be object
			}
		}else{
			if(*seq){
				if((*seq)->type==2){
					(*seq)->value->type=1;
					(*seq)->value->object=O;
				}else return 4; //Object value cannot be object
			}
		}
	}else{ //Value
		V=value_new();
		if(!V) return 5;
		S->value=V;
		if(next){
			S->array=1;
			if(*seq){
				if((*seq)->type==2){
//					(*seq)->value->type=2;
					(*seq)->value->next=V;
				}else return 6; //Object next cannot be value
			}
		}else{
			if(*seq){
				if((*seq)->type==1) (*seq)->object->value=V;
				else{
					(*seq)->value->type=2;
					(*seq)->value->value=V;
				}
			}
		}
	}
	*seq=S;
	return 0;
}

int seq_next(struct sequence **seq,uint8_t next){
	struct sequence *S;
	struct json_object *O=0;
	struct json_value *V=0;

	if(*seq){
		if(next){
			if(!(((*seq)->visited)&0x2)){
				if((*seq)->type==1){
					O=(*seq)->object->next;
				}else if((*seq)->type==2){
					V=(*seq)->value->next;
				}
				(*seq)->visited|=0x2;
			}else return 1;
		}else{
			if(!(((*seq)->visited)&0x1)){
				if((*seq)->type==1){
					V=(*seq)->object->value;
				}else if((*seq)->type==2){
					if((*seq)->value->type==1){
						O=(*seq)->value->object;
					}else if((*seq)->value->type==2){
						V=(*seq)->value->value;
					}
				}
				(*seq)->visited|=0x1;
			}else return 2;
		}
		if(O || V){
			S=seq_new();
			if(!S) return 4;
			(*seq)->next=S;
			S->prev=*seq;
			if(O){
				S->type=1;
				S->object=O;
			}else{
				S->type=2;
				S->value=V;
			}
			(*seq)=S;
			return 0;
		}
		return 3;
	}
	return 5;
}

int seq_prev(struct sequence **seq,uint8_t type){
	struct sequence *S;
	if(*seq){
		S=*seq;
		if((type!=0)&&(S->type!=type)) return 1;
		*seq=S->prev;
		free(S);
		if(*seq){
			(*seq)->next=0;
			return 0;
		}
		return 2;
	}
	return 3;
}

int seq_prev_free(struct sequence **seq,uint8_t type){
	struct sequence *S;
	if(*seq){
		S=*seq;
		if((type!=0)&&(S->type!=type)) return 1;
		*seq=S->prev;
		if(S->type==1){
			if(S->object){
				if(S->object->name) free(S->object->name);
				free(S->object);
			}
		}else{
			if(S->value){
				if((S->value->type==3)&&(S->value->value_string)) free(S->value->value_string);
				free(S->value);
			}
		}
		free(S);
		if(*seq){
			(*seq)->next=0;
			return 0;
		}
		return 2;
	}
	return 3;
}

void free_read_sequence(struct sequence **R){
	struct sequence *R1;
	while(*R){
		R1=*R;
		*R=(*R)->prev;
		free(R1);
	}
}

int process_json(FILE *F,uint8_t *buff,size_t bytes,struct json_start **A,uint8_t file_used){
	uint8_t ending=0;
	uint8_t unicode_array[6],quotes=0,escape_char=0,hex_char=0,hex_unicode=0,oct_char=0,value=0,first_run;
	uint32_t unicode_char;
	size_t i,end_whitespace=0,line=1,line_char=0;
	int ret;
	char char_buf[8];
	struct sequence *read_sequence=0;
	struct buffer *str_value,*str_value_p;

	str_value=buffer_new();
	if(!str_value){
		ending=1;
		fprintf(stderr,"Couldn't create bufer buffer_new()\n");
		goto err;
	}
	str_value_p=str_value;

	if(!(*A)){
		ALLOC(*A,1);
		if(!(*A)){
			ending=2;
			fprintf(stderr,"Error malloc A\n");
			goto err;
		}
	}
	(*A)->type=0;

	if(file_used){
		ALLOC(buff,buff_n);
		if(!buff){
			ending=3;
			fprintf(stderr,"Error malloc buff\n");
			goto err;
		}
		first_run=0;
	}else first_run=1;

	while(first_run||(file_used&&(!feof(F)))){
		if(file_used) bytes=fread(buff,sizeof *buff,buff_n,F);
		for(i=0;i<bytes;i++){
			line_char++;
			if(escape_char){
				if(hex_char){
					if(buff[i]>='0'){
						if(buff[i]<='9'){
							char_buf[--hex_char]=buff[i]-'0';
						}else if(buff[i]>='A'){
							if(buff[i]<='F'){
								char_buf[--hex_char]=buff[i]-'A'+10;
							}else if(buff[i]>='a'){
								if(buff[i]<='f'){
									char_buf[--hex_char]=buff[i]-'a'+10;
								}else{
									ending=4;
									fprintf(stderr,"Error HEX >f\n");
									goto err;
								}
							}else{
								ending=5;
								fprintf(stderr,"Error HEX char F-a\n");
								goto err;
							}
						}else{
							ending=6;
							fprintf(stderr,"Error HEX char 9-A\n");
							goto err;
						}
					}else{
						ending=7;
						fprintf(stderr,"Error HEX char <0\n");
						goto err;
					}
					if(!hex_char){
						buff_add(&str_value_p,char_buf[0]|(char_buf[1]<<4));
						escape_char=0;
					}
				}else if(hex_unicode){
					if(buff[i]>='0'){
						if(buff[i]<='9'){
							char_buf[--hex_unicode]=buff[i]-'0';
						}else if(buff[i]>='A'){
							if(buff[i]<='F'){
								char_buf[--hex_unicode]=buff[i]-'A'+10;
							}else if(buff[i]>='a'){
								if(buff[i]<='f'){
									char_buf[--hex_unicode]=buff[i]-'a'+10;
								}else{
									ending=8;
									fprintf(stderr,"Error Unicode HEX >f\n");
									goto err;
								}
							}else{
								ending=9;
								fprintf(stderr,"Error Unicode HEX char F-a\n");
								goto err;
							}
						}else{
							ending=10;
							fprintf(stderr,"Error Unicode HEX char 9-A\n");
							goto err;
						}
					}else{
						ending=11;
						fprintf(stderr,"Error Unicode HEX char <0\n");
						goto err;
					}
					if(!hex_unicode){
						unicode_char=(uint32_t)char_buf[0]|(char_buf[1]<<4)|(char_buf[2]<<8)|(char_buf[3]<<12)|(char_buf[4]<<16)|(char_buf[5]<<20)|(char_buf[6]<<24)|(char_buf[7]<<28);
						ret=utf32_to_utf8(unicode_char,unicode_array,6);
						if(!ret){
							ending=12;
							fprintf(stderr,"Couldn't decode unicode utf32_to_utf8: %d\n",ret);
							goto err;
						}
						ret=buff_add_array(&str_value_p,unicode_array,6);
						if(ret){
							ending=13;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}
				}else if(oct_char){
					if((buff[i]>='0')&&(buff[i]<='7')){
						char_buf[--oct_char]=buff[i]-'0';
						if(!oct_char){
							ret=buff_add(&str_value_p,char_buf[0]|(char_buf[1]<<3)|(char_buf[2]<<6));
							if(ret){
								ending=14;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
							escape_char=0;
						}
					}else{
						ending=15;
						fprintf(stderr,"Error OCT char not in 0-7: %d\n",ret);
						goto err;
					}
				}else{
					if(buff[i]=='n'){
						ret=buff_add(&str_value_p,'\n');
						if(ret){
							ending=16;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='t'){
						ret=buff_add(&str_value_p,'\t');
						if(ret){
							ending=17;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='r'){
						ret=buff_add(&str_value_p,'\r');
						if(ret){
							ending=18;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='a'){
						ret=buff_add(&str_value_p,'\a');
						if(ret){
							ending=19;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='b'){
						ret=buff_add(&str_value_p,'\b');
						if(ret){
							ending=20;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='v'){
						ret=buff_add(&str_value_p,'\v');
						if(ret){
							ending=21;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='f'){
						ret=buff_add(&str_value_p,'\f');
						if(ret){
							ending=22;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='0'){
						ret=buff_add(&str_value_p,0);
						if(ret){
							ending=23;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}else if(buff[i]=='x'){ // hex
						hex_char=2;
					}else if((buff[i]>='0')&&(buff[i]<='3')){ // octal
						char_buf[2]=buff[i]-'0';
						oct_char=2;
					}else if(buff[i]=='u'){ // unicode 2B
						for(hex_unicode=7;hex_unicode>=4;hex_unicode--) char_buf[hex_unicode]=0;
						hex_unicode++;
					}else if(buff[i]=='U'){ // unicode 4B
						hex_unicode=8;
					}else{
						if(buff[i]=='\n'){
							line++;
							line_char=0;
						}
						ret=buff_add(&str_value_p,buff[i]);
						if(ret){
							ending=24;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						escape_char=0;
					}
				}
			}else{
				if(quotes){
					if(buff[i]=='\"'){
						quotes=0;
					}else if(buff[i]=='\\'){
						escape_char=1;
					}else{
						if(buff[i]=='\n'){
							line++;
							line_char=0;
						}
						ret=buff_add(&str_value_p,buff[i]);
						if(ret){
							ending=25;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
				}else{
					if(buff[i]=='{'){ // ---- { ------
						value=0;
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,1,0);
							if(ret){
								ending=26;
								fprintf(stderr,"Couldn't add next sequence seq_next_new(): %d\n",ret);
								goto err;
							}
							(*A)->object=read_sequence->object;
							(*A)->type=1;
						}else{
							ret=seq_next_new(&read_sequence,1,0);
							if(ret){
								ending=27;
								fprintf(stderr,"Couldn't add next sequence seq_next_new(): %d\n",ret);
								goto err;
							}
						}
						buff_delete(str_value,&str_value_p);
						end_whitespace=0;
					}else if(buff[i]=='}'){ // ---- } ------
						if(read_sequence->array) fprintf(stderr,"Error end of object, not array, %d %zu:%zu\n",read_sequence->type,line,line_char); //Error
						if(value){
							if(read_sequence->type==1){
//								if(read_sequence->object->name) fprintf(stderr,"BAD1: %s\n",read_sequence->object->name);
//								else fprintf(stderr,"BAD!\n");
							}else{
								if(read_sequence->value->type==0){
									ret=value_str(read_sequence->value,str_value,end_whitespace);
									if(ret){
										ending=28;
										fprintf(stderr,"Couldn't add value value_str(): %d\n",ret);
										goto err;
									}
								}else if(buff_length(str_value,end_whitespace)>0){
									ending=29;
									fprintf(stderr,"Error additional text %zu:%zu\n",line,line_char);
									goto err;
								}
							}
							ret=seq_prev(&read_sequence,2);
							if(ret>2){
								ending=30;
								fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
								goto err;
							}
						}else{
							if(buff_length(str_value,end_whitespace)>0){
								ending=31;
								fprintf(stderr,"Error object without value %zu:%zu\n",line,line_char); //Error
								goto err;
							}else{ //Empty object
								ret=object_str(read_sequence->object,str_value,end_whitespace);
								if(ret){
									ending=32;
									fprintf(stderr,"Couldn't add object object_str(): %d\n",ret);
									goto err;
								}
							}
						}
						buff_delete(str_value,&str_value_p);
						end_whitespace=0;
						while(read_sequence->visited){
							ret=seq_prev(&read_sequence,1);
							if(ret>2){
								ending=33;
								fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
								goto err;
							}
						}
						ret=seq_prev(&read_sequence,1);
						if(ret>2){
							ending=34;
							fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
							goto err;
						}
						value=1;
					}else if(buff[i]=='['){ // ---- [ ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,2,0);
							if(ret){
								ending=35;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
							(*A)->value=read_sequence->value;
							(*A)->type=2;
						}else{
							if(read_sequence->array){
								ret=seq_next_new(&read_sequence,2,0);
								if(ret){
									ending=36;
									fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
									goto err;
								}
							}
						}
						read_sequence->array=1;
						value=1;
						buff_delete(str_value,&str_value_p);
						end_whitespace=0;
					}else if(buff[i]==']'){ // ---- ] ------
						if(read_sequence->array){
							if(read_sequence->value->type==0){
								ret=value_str(read_sequence->value,str_value,end_whitespace);
								if(ret){
									ending=37;
									fprintf(stderr,"Couldn't set value string value_str(): %d\n",ret);
									goto err;
								}
							}else if(buff_length(str_value,end_whitespace)>0){
								ending=38;
								fprintf(stderr,"Error additional text %zu:%zu\n",line,line_char);
								goto err;
							}
						}else{
							ret=object_str(read_sequence->object,str_value,end_whitespace);
							if(ret){
								ending=39;
								fprintf(stderr,"Couldn't set object string object_str(): %d\n",ret);
								goto err;
							}
						}
						buff_delete(str_value,&str_value_p);
						end_whitespace=0;
						while(read_sequence->visited){
							ret=seq_prev(&read_sequence,2);
							if(ret>2){
								ending=40;
								fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
								goto err;
							}
						}
						ret=seq_prev(&read_sequence,2);
						if(ret>2){
							ending=41;
							fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
							goto err;
						}
						value=1;
					}else if(buff[i]==':'){ // ---- : ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,1,0);
							if(ret){
								ending=42;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
							(*A)->object=read_sequence->object;
							(*A)->type=1;
						}
						if(read_sequence->array==0){
							value=1;
							ret=object_str(read_sequence->object,str_value,end_whitespace);
							if(ret){
								ending=43;
								fprintf(stderr,"Couldn't set object string object_str(): %d\n",ret);
								goto err;
							}
							buff_delete(str_value,&str_value_p);
							end_whitespace=0;
							ret=seq_next_new(&read_sequence,2,0);
							if(ret){
								ending=44;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
						}else{
							end_whitespace=0;
							ret=buff_add(&str_value_p,buff[i]);
							if(ret){
								ending=45;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
						}
					}else if(buff[i]==','){ // ---- , ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,2,0);
							if(ret){
								ending=46;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
							(*A)->value=read_sequence->value;
							(*A)->type=2;
							read_sequence->array=1;
							value=1;
						}
						if(read_sequence->type==2){
							if(read_sequence->value->type==0){
								ret=value_str(read_sequence->value,str_value,end_whitespace);
								if(ret){
									ending=47;
									fprintf(stderr,"Couldn't set value string value_str(): %d\n",ret);
									goto err;
								}
							}
						}else if(value==0){
							ending=48;
							printf("Error object name without value %zu:%zu\n",line,line_char); //Error
							goto err;
						}
						buff_delete(str_value,&str_value_p);
						end_whitespace=0;
						if(read_sequence->array){
							ret=seq_next_new(&read_sequence,2,1);
							if(ret){
								ending=49;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
						}else{
//							while(read_sequence->visited){
//								ret=seq_prev(&read_sequence,2);
//							}
							ret=seq_prev(&read_sequence,2);
							if(ret>2){
								ending=50;
								fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret);
								goto err;
							}
							ret=seq_next_new(&read_sequence,1,1);
							if(ret){
								ending=51;
								fprintf(stderr,"Couldn't create new sequence seq_next_new(): %d\n",ret);
								goto err;
							}
							value=0;
						}
						read_sequence->visited=1;
					}else if(buff[i]=='\"'){
						end_whitespace=0;
						quotes=1;
					}else if(buff[i]=='\\'){
						end_whitespace=0;
						escape_char=1;
					}else if(buff[i]=='\n'){
						line++;
						line_char=0;
						if(buff_length(str_value,end_whitespace)>0){
							ret=buff_add(&str_value_p,buff[i]);
							if(ret){
								ending=52;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
							end_whitespace++;
						}
					}else if((buff[i]==' ')||(buff[i]=='\t')||(buff[i]=='\r')){
						if(buff_length(str_value,end_whitespace)>0){
							ret=buff_add(&str_value_p,buff[i]);
							if(ret){
								ending=53;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
							end_whitespace++;
						}
					}else{
						end_whitespace=0;
						ret=buff_add(&str_value_p,buff[i]);
						if(ret){
							ending=54;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
				}
			}
		}
		first_run=0;
	}
err:
	switch(ending){
		case 0:
			// fall through
		case 5 ... 54:
			// fall through
		case 4:
			if(file_used) free(buff);
			// fall through
		case 3:
			free_read_sequence(&read_sequence);
			// fall through
		case 2:
			buff_delete(str_value,&str_value_p);
			free(str_value);
			// fall through
		case 1:
			// fall through
		default:
			;
	}
	return ending;
}

int read_json(FILE *F,struct json_start **A){
	uint8_t *buff=0;
	size_t n=0;
	return process_json(F,buff,n,A,1);
}

int read_json_buff(uint8_t *buff,size_t n,struct json_start **A){
	FILE *F=0;
	return process_json(F,buff,n,A,0);
}

int print_struct_buff(struct buffer **B,struct sequence *A,uint8_t *M){
	uint8_t ending=0;
	int ret;

	if(A){
//		printf("t%i",A->type);
		if(A->type==1){
			if(A->object){
				if(A->object->name){
					if(*M){
						*M=0;
						ret=buff_add(B,',');
						if(ret){
							ending=1;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
					*M=0;
					ret=buff_add(B,'\"');
					if(ret){
						ending=2;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
					ret=buff_add_array_s(B,A->object->name,A->object->n);
					if(ret){
						ending=3;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
					ret=buff_add_array_s(B,"\":",2);
					if(ret){
						ending=4;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
				}else{
//					ending=5;
					fprintf(stderr,"No object name\n");
//					goto err;
					if(*M){
						*M=0;
						ret=buff_add(B,',');
						if(ret){
							ending=6;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
					ret=buff_add_array_s(B,"\"\":",3);
					if(ret){
						ending=7;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
				}
			}else{
				ending=8;
				fprintf(stderr,"No object in sequence A\n");
				goto err;
			}
		}else if(A->type==2){
			if(A->value){
				if(A->value->next && (A->prev->type==1)){
					ret=buff_add(B,'[');
					if(ret){
						ending=9;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
				}
//				printf("v%i",A->value->type);
				if(A->value->type==1){
					if(*M){
						*M=0;
						ret=buff_add(B,',');
						if(ret){
							ending=10;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
					ret=buff_add(B,'{');
					if(ret){
						ending=11;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
				}else if(A->value->type==2){
					if(*M){
						*M=0;
						ret=buff_add(B,',');
						if(ret){
							ending=12;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
					ret=buff_add(B,'[');
					if(ret){
						ending=13;
						fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
						goto err;
					}
				}else{
					if(A->value->type==3){
						*M=0;
						ret=buff_add(B,'\"');
						if(ret){
							ending=14;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						ret=buff_add_array_s(B,A->value->value_string,A->value->n);
						if(ret){
							ending=15;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
						ret=buff_add(B,'\"');
						if(ret){
							ending=16;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}else if(A->value->type==4){
						*M=0;
//						fprintf(F,PRId64,A->value->value_int);
					}else if(A->value->type==5){
						*M=0;
//						fprintf(F,"%f",A->value->value_double);
					}else if(A->value->type==6){
						if(A->value->value_boolean==0){
							ret=buff_add_array_s(B,"false",5);
							if(ret){
								ending=19;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
						}else if(A->value->value_boolean==1){
							ret=buff_add_array_s(B,"true",4);
							if(ret){
								ending=20;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
						}else if(A->value->value_boolean==2){
							ret=buff_add_array_s(B,"null",4);
							if(ret){
								ending=21;
								fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
								goto err;
							}
						}
						*M=0;
					}else{
//						ending=22;
						fprintf(stderr,"Unsupported value type: %d\n",A->value->type);
						ret=buff_add_array_s(B,"\"\"",2);
						if(ret){
							ending=23;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
//						goto err;
					}
					if(A->value->next==0){
						if(A->prev){
							if(A->prev->type==1){
								if(A->prev->object->next){
									*M=0;
									ret=buff_add(B,',');
									if(ret){
										ending=24;
										fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
										goto err;
									}
								}else{
//									*M=1;
//									fprintf(F,"}");
//									if(A->prev->next) fprintf(F,",");
								}
							}else if(A->prev->type==2){
								*M=1;
//								fprintf(F,"]");
//								if(A->prev->value->next) fprintf(F,",");
							}
						}else{
//							*M=1;
//							fprintf(F,"]");
						}
					}else{
						*M=0;
						ret=buff_add(B,',');
						if(ret){
							ending=25;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
				}
			}else{
				ending=26;
				fprintf(stderr,"No value in sequence A\n");
				goto err;
			}
		}else{
			ending=27;
			fprintf(stderr,"Unsupported sequence A type: %d\n",A->type);
			goto err;
		}
	}
err:
	switch(ending){
		case 0:
			// fall through
		case 1 ... 27:
			// fall through
		default:
			;
	}
	return ending;
}

int print_struct(FILE *F,struct sequence *A,uint8_t *M){
	uint8_t ending=0;
	if(A){
//		printf("t%i",A->type);
		if(A->type==1){
			if(A->object){
				if(A->object->name){
					if(*M){
						*M=0;
						fprintf(F,",");
					}
					fprintf(F,"\"%s\":",A->object->name);
				}else{
//					ending=1;
					fprintf(stderr,"No object name\n");
//					goto err;
					if(*M){
						*M=0;
						fprintf(F,",");
					}
					fprintf(F,"\"\":");
				}
			}else{
				ending=2;
				fprintf(stderr,"No object in sequence A\n");
				goto err;
			}
		}else if(A->type==2){
			if(A->value){
				if(A->value->next && (A->prev->type==1)) fprintf(F,"[");
//				printf("v%i",A->value->type);
				if(A->value->type==1){
					if(*M){
						*M=0;
						fprintf(F,",");
					}
					fprintf(F,"{");
				}else if(A->value->type==2){
					if(*M){
						*M=0;
						fprintf(F,",");
					}
					fprintf(F,"[");
				}else{
					if(A->value->type==3){
						*M=0;
						fprintf(F,"\"%s\"",A->value->value_string);
					}else if(A->value->type==4){
						*M=0;
						fprintf(F,"%" PRId64,A->value->value_int);
					}else if(A->value->type==5){
						*M=0;
						fprintf(F,"%f",A->value->value_double);
					}else if(A->value->type==6){
						*M=0;
						if(A->value->value_boolean==0) fprintf(F,"false");
						else if(A->value->value_boolean==1) fprintf(F,"true");
						else if(A->value->value_boolean==2) fprintf(F,"null");
					}else{
//						ending=2;
						fprintf(stderr,"Unsupported value type: %d\n",A->value->type);
						fprintf(F,"\"\"");
//						goto err;
					}
					if(A->value->next==0){
						if(A->prev){
							if(A->prev->type==1){
								if(A->prev->object->next){
									*M=0;
									fprintf(F,",");
								}else{
//									*M=1;
//									fprintf(F,"}");
//									if(A->prev->next) fprintf(F,",");
								}
							}else if(A->prev->type==2){
								*M=1;
//								fprintf(F,"]");
//								if(A->prev->value->next) fprintf(F,",");
							}
						}else{
//							*M=1;
//							fprintf(F,"]");
						}
					}else{
						*M=0;
						fprintf(F,",");
					}
				}
			}else{
				ending=2;
				fprintf(stderr,"No value in sequence A\n");
				goto err;
			}
		}else{
			ending=3;
			fprintf(stderr,"Unsupported sequence A type: %d\n",A->type);
			goto err;
		}
	}
err:
	switch(ending){
		case 0:
			// fall through
		case 3:
			// fall through
		case 2:
			// fall through
		case 1:
			// fall through
		default:
			;
	}
	return ending;
}

int print_prev_buff(struct buffer **Buf,struct sequence *A,uint8_t *M){
	uint8_t ending=0;
	int ret;
	struct sequence *B;

	if(A){
		B=A->prev;
		if(B){
			if(A->type==1){
				if(B->type==2){
//					*M=1;
//					fprintf(F,"}");
				}else if(B->type==1){
					if(A->value->next==0){
						*M=1;
						ret=buff_add(Buf,'}');
						if(ret){
							ending=1;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
				}
			}else if(A->type==2){
				if(B->type==1){
//					*M=1;
//					fprintf(F,"}");
				}else if(B->type==2){
					if(B->value->next!=A->value){
						*M=1;
						ret=buff_add(Buf,']');
						if(ret){
							ending=2;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}else if(A->value->next==0){
						*M=1;
						ret=buff_add(Buf,']');
						if(ret){
							ending=3;
							fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
							goto err;
						}
					}
				}
			}
		}else{
//			if(A->type==1) fprintf(F,"}");
//			else if(A->type==2) fprintf(F,"]");
		}
	}
err:
	switch(ending){
		case 0:
			// fall through
		case 3:
		case 2:
		case 1:
		default:
			;
	}
	return ending;
}

int print_prev(FILE *F,struct sequence *A,uint8_t *M){
	uint8_t ending=0;
	struct sequence *B;
	if(A){
		B=A->prev;
		if(B){
			if(A->type==1){
				if(B->type==2){
//					fprintf(F,"}");
//					*M=1;
				}else if(B->type==1){
					if(A->value->next==0){
						*M=1;
						fprintf(F,"}");
					}
				}
			}else if(A->type==2){
				if(B->type==1){
//					*M=1;
//					fprintf(F,"}");
				}else if(B->type==2){
					if(B->value->next!=A->value){
						*M=1;
						fprintf(F,"]");
					}else if(A->value->next==0){
						*M=1;
						fprintf(F,"]");
					}
				}
			}
		}else{
//			if(A->type==1) fprintf(F,"}");
//			else if(A->type==2) fprintf(F,"]");
		}
	}
//err:
	switch(ending){
		case 0:
			// fall through
		default:
			;
	}
	return ending;
}

int print_json_buff(char **buff,size_t *n,struct json_start *A){
	uint8_t ending=0,M=0;
	int ret,ret1,ret2,ret3;
	struct sequence *read_sequence=0;
	struct buffer *B,*B_p;

	B=buffer_new();
	if(!B){
		ending=1;
		fprintf(stderr,"Couldn't create bufer buffer_new()\n");
		goto err;
	}
	B_p=B;

	if(A){
		read_sequence=seq_new();
		if(!read_sequence){
			ending=2;
			fprintf(stderr,"Couldn't create sequence seq_new()\n");
			goto err;
		}
		read_sequence->prev=0;
		read_sequence->type=A->type;
		if(A->type==1){
			read_sequence->object=A->object;
			ret=buff_add(&B_p,'{');
			if(ret){
				ending=3;
				fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
				goto err;
			}
		}else{
			read_sequence->value=A->value;
			ret=buff_add(&B_p,'[');
			if(ret){
				ending=4;
				fprintf(stderr,"Couldn't add bufer buff_add(): %d\n",ret);
				goto err;
			}
		}
		ret3=print_struct_buff(&B_p,read_sequence,&M);
		if(ret3){
			ending=5;
			fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
			goto err;
		}

		ret2=0;
		while(!ret2){
			ret1=0;
			while(!ret1){
				ret=0;
				while(!ret){
					ret=seq_next(&read_sequence,0);
					if(ret>3){
						ending=6;
						fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret);
						goto err;
					}
//					fprintf(F,"c%i",ret);
					if(!ret){
						ret3=print_struct_buff(&B_p,read_sequence,&M);
						if(ret3){
							ending=7;
							fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
							goto err;
						}
					}
				}
				ret1=seq_next(&read_sequence,1);
				if(ret1>3){
					ending=8;
					fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret1);
					goto err;
				}
//				fprintf(F,"b%i",ret1);
				if(!ret1){
					ret3=print_struct_buff(&B_p,read_sequence,&M);
					if(ret3){
						ending=9;
						fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
						goto err;
					}
				}
			}
			print_prev_buff(&B_p,read_sequence,&M);
			ret2=seq_prev(&read_sequence,0);
			if(ret2>2){
				ending=10;
				fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret2);
				goto err;
			}
//			fprintf(F,"a%i",ret2);
			if(!ret2){
				
			}
		}
	}
	*buff=buff_toarray(B,n,0);
	if(!(*buff)){
		ending=11;
		fprintf(stderr,"Couldn't set value string value_str(): %d\n",ret);
		goto err;
	}
err:
	switch(ending){
		case 0:
			// fall through
		case 3 ... 11:
			free_read_sequence(&read_sequence);
			// fall through
		case 2:
			buff_delete(B,&B_p);
			free(B);
			// fall through
		case 1:
			// fall through
		default:
			;
	}
	return ending;
}

int print_json(FILE *F,struct json_start *A){
	uint8_t ending=0,M=0;
	int ret,ret1,ret2,ret3;
	struct sequence *read_sequence=0;

	if(A){
		read_sequence=seq_new();
		if(!read_sequence){
			ending=1;
			fprintf(stderr,"Couldn't create sequence seq_new()\n");
			goto err;
		}
		read_sequence->prev=0;
		read_sequence->type=A->type;
		if(A->type==1){
			read_sequence->object=A->object;
			fprintf(F,"{");
		}else{
			read_sequence->value=A->value;
			fprintf(F,"[");
		}
		ret3=print_struct(F,read_sequence,&M);
		if(ret3){
			ending=2;
			fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
			goto err;
		}

		ret2=0;
		while(!ret2){
			ret1=0;
			while(!ret1){
				ret=0;
				while(!ret){
					ret=seq_next(&read_sequence,0);
					if(ret>3){
						ending=3;
						fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret);
						goto err;
					}
//					fprintf(F,"c%i",ret);
					if(!ret){
						ret3=print_struct(F,read_sequence,&M);
						if(ret3){
							ending=4;
							fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
							goto err;
						}
					}
				}
				ret1=seq_next(&read_sequence,1);
				if(ret1>3){
					ending=5;
					fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret1);
					goto err;
				}
//				fprintf(F,"b%i",ret1);
				if(!ret1){
					ret3=print_struct(F,read_sequence,&M);
					if(ret3){
						ending=6;
						fprintf(stderr,"Couldn't print sequence print_struct(): %d\n",ret3);
						goto err;
					}
				}
			}
			print_prev(F,read_sequence,&M);
			ret2=seq_prev(&read_sequence,0);
			if(ret2>2){
				ending=7;
				fprintf(stderr,"Couldn't get prev sequence seq_prev(): %d\n",ret2);
				goto err;
			}
//			fprintf(F,"a%i",ret2);
			if(!ret2){
				
			}
		}
	}
	fprintf(F,"\n");
err:
	switch(ending){
		case 0:
			// fall through
		case 2 ... 7:
			free_read_sequence(&read_sequence);
			// fall through
		case 1:
			// fall through
		default:
			;
	}
	return ending;
}

int free_json(struct json_start **A){
	uint8_t ending=0;
	int ret,ret1,ret2;
	struct sequence *read_sequence=0;

	if(*A){
		read_sequence=seq_new();
		if(!read_sequence){
			ending=1;
			fprintf(stderr,"Couldn't create sequence seq_new()\n");
			goto err;
		}
		if(!read_sequence) return 1;
		read_sequence->prev=0;
		read_sequence->type=(*A)->type;
		if((*A)->type==1) read_sequence->object=(*A)->object;
		else read_sequence->value=(*A)->value;

		ret2=0;
		while(!ret2){
			ret1=0;
			while(!ret1){
				ret=0;
				while(!ret){
					ret=seq_next(&read_sequence,0);
					if(ret>3){
						ending=2;
						fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret);
						goto err;
					}
//					printf("c%i",ret);
				}
				ret1=seq_next(&read_sequence,1);
				if(ret1>3){
					ending=2;
					fprintf(stderr,"Couldn't get next sequence seq_next(): %d\n",ret1);
					goto err;
				}
//				printf("b%i",ret1);
			}
			ret2=seq_prev_free(&read_sequence,0); //free prev
			if(ret2>2){
				ending=4;
				fprintf(stderr,"Couldn't get prev sequence seq_prev_free(): %d\n",ret2);
				goto err;
			}
//			printf("a%i",ret2);
		}
		free(*A);
		*A=0;
	}
//	printf("\n");
err:
	switch(ending){
		case 0:
			// fall through
		case 4:
			// fall through
		case 3:
			// fall through
		case 2:
			free_read_sequence(&read_sequence);
			// fall through
		case 1:
			if(*A) free(*A);
			// fall through
		default:
			;
	}
	return ending;
}

struct json_object * get_object(char *S,struct json_object *O){
	while(O){
		if(!strncmp(O->name,S,O->n)) return O;
		O=O->next;
	}
	return 0;
}

struct json_value * get_value(char *S,struct json_object *O){
	struct json_object *O1;
	O1=get_object(S,O);
	if(O1) return O1->value;
	return 0;
}

char * get_string_first(char *S,size_t *n,struct json_object *O){
	struct json_value *V1;
	V1=get_value(S,O);
	if(V1&&(V1->type==3)){
		*n=V1->n;
		return V1->value_string;
	}
	return 0;
}
