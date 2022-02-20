
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/*=====================================================*/
/* declarations */
/*=====================================================*/


#define FC_STR_MAX 1024

/* datatype for the json parser */
struct find_card_struct
{
  FILE *fp;
  uint32_t str_cnt;
  int c;        // current char 
  uint32_t n;   // current index number
  int len; // current len of the string
  int max_len; // max len over all strings found
  uint16_t s[FC_STR_MAX];       // unicode string
  //uint16_t m[FC_STR_MAX];       // match string
  //int m_len;    // length of the match string
  uint16_t min_distance;
};
typedef struct find_card_struct fc_t;

#define read_next(fc) (fc)->c = getc((fc)->fp);

/* simple hex conversion without any error checks */
#define from_hex(c) ((c)<='9'?((c)-'0'):((c)-'a'+10))

#define test_0_return_0(fn) if ( fn == 0 ) return 0
//#define test_0_return_0(fn) fn


/* datatype for levenshtein distance and string length (char is always uint16_t) */
typedef int lvint_t;


/*=====================================================*/
/* global variables */
/*=====================================================*/

/* global control */
int find_card_during_parse = 0;
int use_card_name_list = 1;

char *pipe_name = "find_card_pipe";

/* match card uint16 string (written by json_to_match_uint16) */
uint16_t match_uint16_string[FC_STR_MAX];
uint16_t match_uint16_len = 0;

/* cnl_match output */
uint16_t match_distance = 0;
uint32_t match_index = 0;

/* card name list management */
/*
  card_name_list[idx][0] --> card number
  card_name_list[idx][1] --> card name length
*/
uint16_t **card_name_list = NULL;
uint32_t card_name_cnt = 0;
uint32_t card_name_max = 0;


/*=====================================================*/

/*
https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C
*/

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))



lvint_t levenshtein(const uint16_t *s1, lvint_t s1len, const uint16_t  *s2, lvint_t s2len, lvint_t stop_distance) 
{
  lvint_t x, y; 
  lvint_t lastdiag;
  lvint_t olddiag;
  lvint_t current;
  lvint_t colmin;
  lvint_t column[s1len + 1];
  for (y = 1; y <= s1len; y++)
      column[y] = y;
  for (x = 1; x <= s2len; x++) 
  {
    column[0] = x;
    colmin = 0xffff;
    for (y = 1, lastdiag = x - 1; y <= s1len; y++) 
    {
      olddiag = column[y];
      current = MIN3(column[y] + 1, column[y - 1] + 1, lastdiag + (s1[y-1] == s2[x - 1] ? 0 : 1));
      column[y] = current;
      if ( colmin > current )
        colmin = current;
      lastdiag = olddiag;
    }
    if ( colmin > stop_distance )
      return colmin;
  }
  return column[s1len];
}



#define CNL_EXPAND (1024*4)

void cnl_expand(void)
{
  if ( card_name_list == NULL )
  {
    card_name_list = (uint16_t **)malloc(CNL_EXPAND*sizeof(uint16_t *));
    card_name_max = CNL_EXPAND;
  }
  else
  {
    card_name_list = (uint16_t **)realloc(card_name_list, (CNL_EXPAND+card_name_max)*sizeof(uint16_t *));
    card_name_max += CNL_EXPAND;
  }
  if ( card_name_list == NULL )
  {
    exit(1);    // memory error
  }
}

void cnl_add(uint16_t n, uint16_t len, const uint16_t *s)
{
  uint16_t i;
  while( card_name_max <= card_name_cnt )
    cnl_expand();
  
  card_name_list[card_name_cnt] = (uint16_t *)malloc(sizeof(uint16_t)*(len+2));
  if ( card_name_list[card_name_cnt] == NULL )
  {
    exit(1);    // memory error
  }

  card_name_list[card_name_cnt][0] = n;
  card_name_list[card_name_cnt][1] = len;
  for( i = 0; i < len; i++ )
    card_name_list[card_name_cnt][i+2] = s[i];
  card_name_cnt++;
}


uint32_t cnl_match(uint16_t len, const uint16_t *s)
{
  uint32_t i;
  uint32_t best_i = 0;
  uint16_t distance;
  uint16_t min_distance = 0xffff;
  for( i = 0; i < card_name_cnt; i++ )
  {
      distance = levenshtein(card_name_list[i]+2, card_name_list[i][1], s, len, min_distance);
      if ( min_distance > distance )
      {
        min_distance = distance;
        best_i = i;
        printf("%d ", min_distance);
      }      
  }
  printf("\n");

  match_distance = min_distance;
  match_index = best_i;
  
  //printf("len=%d best_i=%d min_distance=%d\n", len, best_i, min_distance);
  return best_i;
}

/*=====================================================*/

void show_match_uint16(void)
{
  int i;
  for( i = 0; i < match_uint16_len; i++ )
  {
    printf("%02d: '%c' %04x\n", i,  match_uint16_string[i],  match_uint16_string[i]);
  }
}

/*
  convert a 16 byte string array to JSON
*/

const char *uint16_to_json(uint16_t len, const uint16_t *s)
{
  static char buf[FC_STR_MAX];
  uint16_t i, j;
  j = 0;
  for ( i = 0; i < len; i++ )
  {
    if ( s[i] < 128 )
    {
      buf[j] = s[i];
      j++;
    }
    else
    {
      j+=sprintf(buf+j, "\\u%04x", s[i]);
    }
  } 
  return buf;
}

/*

  Expects a json string without double quotes
  This will read the string and convert the string to uint16 values
  Parsings stops at '\0' and '\"'

  same as "int read_str(fc_t *fc)" but will read from a string instead

  Result is stored in
    uint16_t match_uint16_string[FC_STR_MAX];
    uint16_t match_uint16_len = 0;

*/
int json_to_match_uint16(const char *json_str)
{
  const char *s = json_str;
  int i = 0;
  
  for(;;)
  {
    if ( *s == '\\' )
    {
      // read escape sequence
      s++;      // read next char into *s
      if ( *s == 'u' )
      {
        uint16_t v = 0;
        s++;      // read next char into *s
        
        v += from_hex(*s);
        s++;      // read next char into *s
        
        v *= 16;
        v += from_hex(*s);
        s++;      // read next char into *s
        
        v *= 16;
        v += from_hex(*s);
        s++;      // read next char into *s
        
        v *= 16;
        v += from_hex(*s);
        s++;      // read next char into *s
        
        match_uint16_string[i] = v;        // store the unicode encoding
      }
      else if ( *s == '\"' )
      {
        match_uint16_string[i] = *s;       // store current char 
        s++;      // read next char into *s
      }
      else
      {
        //printf("Unknown str escape %c\n", *s);
        return 0;
      }
      
    }
    else if ( *s == '\0' )
    {
      match_uint16_string[i] = '\0';        // end of file found: terminate string and return with error
      break;
    }
    else if ( *s == '\"' )
    {
      match_uint16_string[i] = '\0';        // end of string found: terminate string and return
      s++;      // read next char into *s
      break;
    }
    else
    {        
      match_uint16_string[i] = *s;       // store current char 
      s++;      // read next char into *s
    }
    i++;      // char is done, start with next char
  } // for
  
  match_uint16_len = i;
  //printf("match_uint16_len=%d\n", match_uint16_len);
  return 1;
}

/*=====================================================*/


int skip_space(fc_t *fc)
{
  for(;;)
  {
    if ( fc->c > 32 )
      break;
    if ( fc->c < 0 )
      return 0;
    read_next(fc);      // read next char into fc->c
  }
  return 1;
}


int read_str(fc_t *fc)
{
  int i = 0;
  if ( fc->c == '\"' )
  {
    read_next(fc);      // read next char into fc->c
    for(;;)
    {
      if ( fc->c == '\\' )
      {
        // read escape sequence
        read_next(fc);      // read next char into fc->c
        if ( fc->c == 'u' )
        {
          uint16_t v = 0;
          read_next(fc);      // read next char into fc->c
          
          v += from_hex(fc->c);
          read_next(fc);      // read next char into fc->c
          
          v *= 16;
          v += from_hex(fc->c);
          read_next(fc);      // read next char into fc->c
          
          v *= 16;
          v += from_hex(fc->c);
          read_next(fc);      // read next char into fc->c
          
          v *= 16;
          v += from_hex(fc->c);
          read_next(fc);      // read next char into fc->c
          
          fc->s[i] = v;        // store the unicode encoding
        }
        else if ( fc->c == '\"' )
        {
          fc->s[i] = fc->c;       // store current char 
          read_next(fc);      // read next char into fc->c
        }
        else
        {
          //printf("Unknown str escape %c\n", fc->c);
          return 0;
        }
        
      }
      else if ( fc->c < 0 )
      {
        fc->s[i] = '\0';        // end of file found: terminate string and return with error
        return 0;
      }
      else if ( fc->c == '\"' )
      {
        fc->s[i] = '\0';        // end of string found: terminate string and return
        read_next(fc);      // read next char into fc->c
        break;
      }
      else
      {        
        fc->s[i] = fc->c;       // store current char 
        read_next(fc);      // read next char into fc->c
      }
      i++;      // char is done, start with next char
    } // for
  }
  fc->len = i;
  if ( fc->max_len < i )
    fc->max_len = i;
  fc->str_cnt++;
  return 1;
}

int read_number(fc_t *fc)
{
  fc->n = 0;
  for(;;)
  {
    if ( fc->c >= '0' && fc->c <= '9' )
    {
      fc->n *= 10;
      fc->n += fc->c-'0';      
    }
    else if ( fc->c < 0 )
    {
      return 0;
    }
    else
    {
      break;
    }
    read_next(fc);      // read next char into fc->c
  }
  test_0_return_0(skip_space(fc));
  return 1;
}

int read_dic(fc_t *fc)
{
  uint16_t distance;
  if ( fc->c == '{' )
  {
    read_next(fc);      // read next char into fc->c
    if ( skip_space(fc) == 0 )
      return 0;
    for( ;; )
    {
      if ( fc->c < 0 )
        return 0;
      if ( fc->c == '}' )
      {
        read_next(fc);      // read next char into fc->c
        test_0_return_0(skip_space(fc));
        break;
      }
      test_0_return_0( read_str(fc) );
      test_0_return_0(skip_space(fc));
      if ( fc->c != ':' )
      {
        printf("current char = %c\n", fc->c);
        return puts("':' expected"), 0;
      }
      read_next(fc);      // read next char into fc->c
      test_0_return_0(skip_space(fc));
      test_0_return_0( read_number(fc));
      if ( fc->c == ',' )
      {
        read_next(fc);      // read next char into fc->c
        test_0_return_0(skip_space(fc));
      }
      
      if ( use_card_name_list )
      {
        cnl_add(fc->n, fc->len, fc->s);
      }
      
      if ( find_card_during_parse != 0 && match_uint16_len > 0 )
      {
        distance = levenshtein(fc->s, fc->len, match_uint16_string, match_uint16_len, fc->min_distance);
        //distance = levenshtein(fc->m, fc->m_len, fc->s, fc->len, fc->min_distance);
        if ( fc->min_distance > distance )
        {
          fc->min_distance = distance;
          printf("distance: %d\n", fc->min_distance);
        }
      }
    }
  }
  return 1;
}


int read_fp(fc_t *fc)
{
  fc->str_cnt = 0;
  read_next(fc);      // read first char into fc->c
  skip_space(fc);
  if ( read_dic(fc) == 0 )
    return 0;
  return 1;
}

char read_buffer[BUFSIZ*8];  // ~ 1%-2% improvement 

/*
  filename: The JSON dictionary with all card names (key) and index numbers (value)

  global variables:
    use_card_name_list              store all card names in the card name list table
    find_card_during_parse      Do a distance match during parsing

*/
void read_file(const char *filename)
{
  fc_t fc;
  
  printf("reading card dictionary '%s'\n", filename);
  
  fc.fp = fopen(filename, "r");
  if ( fc.fp != NULL )
  {    
    fc.min_distance = 0xffff;
    fc.max_len = 0;
    setvbuf(fc.fp, read_buffer, _IOFBF, BUFSIZ*8);
    read_fp(&fc);
    fclose(fc.fp);
    
    //printf("str_cnt=%u\n", fc.str_cnt);
    //printf("max_len=%d\n", fc.max_len);    
  }
}

void remove_pipe(void)
{
  unlink(pipe_name);
}


int prepare_pipe(void)
{
  
  printf("using named pipe '%s'\n", pipe_name);
  
  if ( mkfifo(pipe_name, 0666) != 0 )
  {
    if ( errno == EEXIST )
      return 1; // let's hope that this is the right file
    
    perror(pipe_name);
    return 0;
  }
  
  return 1;
}

/*
  expect a json string (MUST start with double quote) or "quit"
  

  will sent:
  [card number/dictionary value, card name, distance]
*/
int wait_and_process_pipe(void)
{
  static char buf[FC_STR_MAX];
  FILE *fp;
  char *s;

  fp = fopen(pipe_name, "r");
  if ( fp == NULL )
  {
    perror(pipe_name);
    return 0;
  }
  
  s = fgets(buf, FC_STR_MAX, fp);
  if ( s == NULL )
  {
    perror(pipe_name);
    fclose(fp);
    return 0;
  }

  fclose(fp);
  
  if ( s[0] == '\0'  )
  {
    /* empty string received */
    printf("Received empty string on '%s'\n", pipe_name);
    return 0;
  }
  
  if ( s[0] != '\"' )
  {
    /* no double quote, assuming quit */
    printf("Received <%s> without double quote\n", s);
    return 0;
  }
  
  printf("Received %s\n", s);
  json_to_match_uint16(s+1);            // skip the first double quote
  //show_match_uint16();
  
  cnl_match(match_uint16_len, match_uint16_string);

  /* results from cnl_match() are also in global variables: 
    match_distance;
    match_index;
  */

  //printf("best_match_index = %d\n", best_match_index);
  
  //printf("card_name_list[best_match_index][0] = %d\n", card_name_list[best_match_index][0]);
  //printf("card_name_list[best_match_index][1] = %d\n", card_name_list[best_match_index][1]);
  
  printf("[%d, \"%s\", %d]\n", 
    card_name_list[match_index][0], 
    uint16_to_json(card_name_list[match_index][1], card_name_list[match_index]+2), 
    match_distance);

  fp = fopen(pipe_name, "w");
  if ( fp == NULL )
  {
    perror(pipe_name);
    return 0;
  }

  fprintf(fp, "[%d, \"%s\", %d]\n", 
    card_name_list[match_index][0], 
    uint16_to_json(card_name_list[match_index][1], card_name_list[match_index]+2), 
    match_distance);  

  fclose(fp);

  return 1;
}

int main(int argc, char **argv)
{
  read_file("mtg_card_dic.json");
  
  if ( prepare_pipe() == 0 )
    return 1;
  
  while( wait_and_process_pipe() != 0 )
    ;
  
  /*
  if ( argc <= 1 )
    return 0;
  read_file(argv[1]);
  */
  remove_pipe();
  return 0;
}
