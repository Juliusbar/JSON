/*
 * Copyright (c) 2018 Julius Barzdziukas <julius.barzdziukas@gmail.com>
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
 * 
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
	n-=w;
	return n;
}

char * buff_toarray(struct buffer *P,size_t w){
	size_t n,i,j;
	char *str;
	n=buff_length(P,w);
	ALLOC(str,n+1);
	if(!str) return 0;
	i=0;
	while(P){
		for(j=0;(j<P->n)&&(i<n);i++,j++) str[i]=P->s[j];
		P=P->next;
	}
	str[n]=0;
	return str;
}

struct json_object * object_new(){
	struct json_object *O;
	ALLOC(O,1);
	if(O){
		O->next=0;
		O->value=0;
		O->name=0;
	}
	return O;
}

int object_str(struct json_object *O,struct buffer *S, size_t w){
	O->name=buff_toarray(S,w);
	return 0;
}

struct json_value * value_new(){
	struct json_value *V;
	ALLOC(V,1);
	if(V){
		V->type=0;
		V->next=0;
		V->value_string=0;
	}
	return V;
}

int value_str(struct json_value *V,struct buffer *S,size_t w){
	V->type=3;
	V->value_string=buff_toarray(S,w);
	return 0;
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
			if(!S) return 3;
			if(*seq){
				(*seq)->next=S;
				S->prev=*seq;
			}else{
				S->prev=0;
			}
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
		return 4;
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

int read_json(FILE *F,struct json_start **A){
	uint8_t *buff,quotes=0,escape_char=0,hex_char=0,hex_unicode=0,oct_char=0,value=0;
	uint32_t unicode_char;
	size_t bytes,i,end_whitespace=0,line=1,line_char=0;
	int ret;
	char char_buf[8];
	struct sequence *read_sequence=0;
	struct buffer *str_value,*str_value_p;

	str_value=buffer_new();
	if(!str_value) return 1;
	str_value_p=str_value;

	if(!(*A)){
		ALLOC(*A,1);
		if(!(*A)) return 2;
	}
	(*A)->type=0;

	ALLOC(buff,buff_n);
	if(!buff) return 3;

	while (!feof(F)){
		bytes=fread(buff,sizeof *buff,buff_n,F);
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
								}else{  //Error HEX >f
								}
							}else{ //Error HEX char F-a
							}
						}else{ //Error HEX char 9-A
						}
					}else{ //Error HEX char <0
					}
					if(!hex_char){
						buff_add(&str_value,char_buf[0]|(char_buf[1]<<4));
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
								}else{  //Error Unicode HEX >f
								}
							}else{ //Error Unicode HEX char F-a
							}
						}else{ //Error Unicode HEX char 9-A
						}
					}else{ //Error Unicode HEX char <0
					}
					if(!hex_unicode){
						unicode_char=(uint32_t)char_buf[0]|(char_buf[1]<<4)|(char_buf[2]<<8)|(char_buf[3]<<12)|(char_buf[4]<<16)|(char_buf[5]<<20)|(char_buf[6]<<24)|(char_buf[7]<<28);
						if(unicode_char<=0x7F){
							buff_add(&str_value,unicode_char);
						}else if(unicode_char<=0x7FF){
							buff_add(&str_value,(unicode_char>>6)|0xC0);
							buff_add(&str_value,(unicode_char&0x3F)|0x80);
						}else if(unicode_char<=0xFFFF){
							buff_add(&str_value,(unicode_char>>12)|0xE0);
							buff_add(&str_value,((unicode_char>>6)&0x3F)|0x80);
							buff_add(&str_value,(unicode_char&0x3F)|0x80);
						}else if(unicode_char<=0x1FFFFF){
							buff_add(&str_value,(unicode_char>>18)|0xF0);
							buff_add(&str_value,((unicode_char>>12)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>6)&0x3F)|0x80);
							buff_add(&str_value,(unicode_char&0x3F)|0x80);
						}else if(unicode_char<=0x3FFFFFF){
							buff_add(&str_value,(unicode_char>>24)|0xF8);
							buff_add(&str_value,((unicode_char>>18)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>12)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>6)&0x3F)|0x80);
							buff_add(&str_value,(unicode_char&0x3F)|0x80);
						}else if(unicode_char<=0x7FFFFFFF){
							buff_add(&str_value,(unicode_char>>28)|0xFC);
							buff_add(&str_value,((unicode_char>>24)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>18)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>12)&0x3F)|0x80);
							buff_add(&str_value,((unicode_char>>6)&0x3F)|0x80);
							buff_add(&str_value,(unicode_char&0x3F)|0x80);
						}else{ //Error Unicode char >0x7FFFFFFF
						}
						escape_char=0;
					}
				}else if(oct_char){
					if((buff[i]>='0')&&(buff[i]<='7')){
						char_buf[--oct_char]=buff[i]-'0';
						if(!oct_char){
							buff_add(&str_value,char_buf[0]|(char_buf[1]<<3)|(char_buf[2]<<6));
							escape_char=0;
						}
					}else{ //Error OCT char not in 0-7
					}
				}else{
					if(buff[i]=='n'){
						buff_add(&str_value,'\n');
						escape_char=0;
					}else if(buff[i]=='t'){
						buff_add(&str_value,'\t');
						escape_char=0;
					}else if(buff[i]=='r'){
						buff_add(&str_value,'\r');
						escape_char=0;
					}else if(buff[i]=='a'){
						buff_add(&str_value,'\a');
						escape_char=0;
					}else if(buff[i]=='b'){
						buff_add(&str_value,'\b');
						escape_char=0;
					}else if(buff[i]=='v'){
						buff_add(&str_value,'\v');
						escape_char=0;
					}else if(buff[i]=='f'){
						buff_add(&str_value,'\f');
						escape_char=0;
					}else if(buff[i]=='0'){
						buff_add(&str_value,0);
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
						buff_add(&str_value,buff[i]);
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
						buff_add(&str_value,buff[i]);
					}
				}else{
					if(buff[i]=='{'){ // ---- { ------
						value=0;
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,1,0);
							(*A)->object=read_sequence->object;
							(*A)->type=1;
						}else{
							ret=seq_next_new(&read_sequence,1,0);
						}
						buff_delete(str_value_p,&str_value);
						end_whitespace=0;
					}else if(buff[i]=='}'){ // ---- } ------
						if(read_sequence->array) printf("Error end of object, not array, %d %zd:%zd\n",read_sequence->type,line,line_char); //Error
						if(value){
							if(read_sequence->value->type==0){
								ret=value_str(read_sequence->value,str_value_p,end_whitespace);
							}else if(buff_length(str_value,end_whitespace)>0){
								printf("Error additional text %zd:%zd\n",line,line_char);
							}
							ret=seq_prev(&read_sequence,2);
						}else{
							if(buff_length(str_value,end_whitespace)>0) printf("Error object without value %zd:%zd\n",line,line_char); //Error
							else{ //Empty object
								ret=object_str(read_sequence->object,str_value_p,end_whitespace);
							}
						}
						buff_delete(str_value_p,&str_value);
						end_whitespace=0;
						while(read_sequence->visited) ret=seq_prev(&read_sequence,1);
						ret=seq_prev(&read_sequence,1);
						value=1;
					}else if(buff[i]=='['){ // ---- [ ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,2,0);
							(*A)->value=read_sequence->value;
							(*A)->type=2;
						}else{
							if(read_sequence->array){
								ret=seq_next_new(&read_sequence,2,0);
							}
						}
						read_sequence->array=1;
						value=1;
						buff_delete(str_value_p,&str_value);
						end_whitespace=0;
					}else if(buff[i]==']'){ // ---- ] ------
						if(read_sequence->array){
							if(read_sequence->value->type==0){
								ret=value_str(read_sequence->value,str_value_p,end_whitespace);
							}else if(buff_length(str_value,end_whitespace)>0){
								printf("Error additional text %zd:%zd\n",line,line_char);
							}
						}else{
							ret=object_str(read_sequence->object,str_value_p,end_whitespace);
						}
						buff_delete(str_value_p,&str_value);
						end_whitespace=0;
						while(read_sequence->visited) ret=seq_prev(&read_sequence,2);
						ret=seq_prev(&read_sequence,2);
						value=1;
					}else if(buff[i]==':'){ // ---- : ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,1,0);
							(*A)->object=read_sequence->object;
							(*A)->type=1;
						}
						if(read_sequence->array==0){
							value=1;
							ret=object_str(read_sequence->object,str_value_p,end_whitespace);
							buff_delete(str_value_p,&str_value);
							end_whitespace=0;
							ret=seq_next_new(&read_sequence,2,0);
						}else{
							end_whitespace=0;
							buff_add(&str_value,buff[i]);
						}
					}else if(buff[i]==','){ // ---- , ------
						if((*A)->type==0){
							ret=seq_next_new(&read_sequence,2,0);
							(*A)->value=read_sequence->value;
							(*A)->type=2;
							read_sequence->array=1;
							value=1;
						}
						if(read_sequence->type==2){
							if(read_sequence->value->type==0){
								ret=value_str(read_sequence->value,str_value_p,end_whitespace);
							}
						}else if(value==0){
							printf("Error object name without value %zd:%zd\n",line,line_char); //Error
						}
						buff_delete(str_value_p,&str_value);
						end_whitespace=0;
						if(read_sequence->array){
							ret=seq_next_new(&read_sequence,2,1);
						}else{
//							while(read_sequence->visited) ret=seq_prev(&read_sequence,2);
							ret=seq_prev(&read_sequence,2);
							ret=seq_next_new(&read_sequence,1,1);
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
							buff_add(&str_value,buff[i]);
							end_whitespace++;
						}
					}else if((buff[i]==' ')||(buff[i]=='\t')||(buff[i]=='\r')){
						if(buff_length(str_value,end_whitespace)>0){
							buff_add(&str_value,buff[i]);
							end_whitespace++;
						}
					}else{
						end_whitespace=0;
						buff_add(&str_value,buff[i]);
					}
				}
			}
		}
	}
	buff_delete(str_value_p,&str_value);
	free(str_value_p);
	free(buff);
	free_read_sequence(&read_sequence);
	return 0;
}

void print_struct(FILE *F,struct sequence *A){
	if(A){
//		printf("t%i",A->type);
		if(A->type==1){
			if(A->object->name){
				fprintf(F,"\"%s\":",A->object->name);
			}else if(A->next){
				fprintf(F,",");
			}else{
				fprintf(F,"}");
			}
		}else if(A->type==2){
			if(A->value->next&&(A->prev->type==1)) fprintf(F,"[");
//			printf("v%i",A->value->type);
			if(A->value->type==1){
				fprintf(F,"{");
			}else if(A->value->type==2){
				fprintf(F,"[");
			}else{
				if(A->value->type==3){
					fprintf(F,"\"%s\"",A->value->value_string);
				}else if(A->value->type==4){
					fprintf(F,PRId64,A->value->value_int);
				}else if(A->value->type==5){
					fprintf(F,"%f",A->value->value_double);
				}else if(A->value->type==6){
					if(A->value->value_boolean) fprintf(F,"true");
					else fprintf(F,"false");
				}
				if(A->value->next==0){
					if(A->prev){
						if(A->prev->type==1){
							if(A->prev->object->next) fprintf(F,",");
							else fprintf(F,"}");
						}else if(A->prev->type==2) fprintf(F,"]");
					}else{
						fprintf(F,"]");
					}
				}else{
					fprintf(F,",");
				}
			}
		}
	}
}

int print_json(FILE *F,struct json_start *A){
	int ret,ret1,ret2;
	struct sequence *read_sequence=0;

	if(A){
		read_sequence=seq_new();
		if(!read_sequence) return 1;
		read_sequence->prev=0;
		read_sequence->type=A->type;
		if(A->type==1){
			read_sequence->object=A->object;
			fprintf(F,"{");
		}else{
			read_sequence->value=A->value;
			fprintf(F,"[");
		}
		print_struct(F,read_sequence);

		ret2=0;
		while(!ret2){
			ret1=0;
			while(!ret1){
				ret=0;
				while(!ret){
					ret=seq_next(&read_sequence,0);
//					fprintf(F,"c%i",ret);
					if(!ret) print_struct(F,read_sequence);
				}
				ret1=seq_next(&read_sequence,1);
//				fprintf(F,"b%i",ret1);
				if(!ret1) print_struct(F,read_sequence);
			}
			ret2=seq_prev(&read_sequence,0);
//			fprintf(F,"a%i",ret2);
		}
	}
	fprintf(F,"\n");
	free_read_sequence(&read_sequence);
	return 0;
}

int free_json(struct json_start **A){
	int ret,ret1,ret2;
	struct sequence *read_sequence=0;

	if(*A){
		read_sequence=seq_new();
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
//					printf("c%i",ret);
				}
				ret1=seq_next(&read_sequence,1);
//				printf("b%i",ret1);
			}
			ret2=seq_prev_free(&read_sequence,0); //free prev
//			printf("a%i",ret2);
		}
		free(*A);
		*A=0;
	}
//	printf("\n");
	free_read_sequence(&read_sequence);
	return 0;
}

struct json_value * get_value(char *S,struct json_object *O){
	while(O){
		if(!strcmp(O->name,S)) return O->value;
		O=O->next;
	}
	return 0;
}

char * get_string_first(char *S,struct json_object *O){
	while(O){
		if(!strcmp(O->name,S)){
			if(O->value&&(O->value->type==3)) return O->value->value_string;
			return 0;
		}
		O=O->next;
	}
	return 0;
}
