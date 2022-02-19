
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define FC_STR_MAX 1024

struct find_card_struct
{
  FILE *fp;
  uint32_t str_cnt;
  int c;        // current char 
  uint32_t n;   // current index number
  int len; // current len of the string
  int max_len; // max len over all strings found
  uint16_t s[FC_STR_MAX];       // unicode string
  uint16_t m[FC_STR_MAX];       // match string
  int m_len;    // length of the match string
  uint16_t min_distance;
};
typedef struct find_card_struct fc_t;

#define read_next(fc) (fc)->c = getc((fc)->fp);

/*=====================================================*/

/*
https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C
*/

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))


typedef int lvint_t;

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

/*=====================================================*/

uint16_t **card_name_list = NULL;
uint32_t card_name_cnt = 0;
uint32_t card_name_max = 0;



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
        printf("cnl distance: %d\n", min_distance);
      }
  }
  return best_i;
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

#define from_hex(c) ((c)<='9'?(c)-'0':(c)-'a')
#define test_0_return_0(fn) if ( fn == 0 ) return 0

//#define test_0_return_0(fn) fn

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
      
      cnl_add(fc->n, fc->len, fc->s);
      
      distance = levenshtein(fc->s, fc->len, fc->m, fc->m_len, fc->min_distance);
      //distance = levenshtein(fc->m, fc->m_len, fc->s, fc->len, fc->min_distance);
      if ( fc->min_distance > distance )
      {
        fc->min_distance = distance;
        printf("distance: %d\n", fc->min_distance);
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

char read_buffer[BUFSIZ*4];  // ~ 1%-2% improvement 

void read_file(const char *filename, const char *match)
{
  fc_t fc;
  fc.fp = fopen(filename, "r");
  if ( fc.fp != NULL )
  {
    int i = 0;
    while( match[i] != '\0' )
    {
      fc.m[i] = match[i];
      i++;
    }
    fc.m_len = i;
    fc.min_distance = 0xffff;
    fc.max_len = 0;
    setvbuf(fc.fp, read_buffer, _IOFBF, BUFSIZ*16);
    read_fp(&fc);
    fclose(fc.fp);
    printf("str_cnt=%u\n", fc.str_cnt);
    printf("max_len=%d\n", fc.max_len);
    
    for( int i = 0; i < 10; i++ )
      cnl_match(fc.m_len, fc.m);

  }
}

int main(int argc, char **argv)
{
  read_file("mtg_card_dic.json", "Thron von Makindi");
  /*
  if ( argc <= 1 )
    return 0;
  read_file(argv[1]);
  */
  return 0;
}
