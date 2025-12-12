#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#ifndef TAB_WIDTH
#define TAB_WIDTH 4
#endif

#ifdef _WIN32
  #include <io.h>
  #define popen  _popen
  #define pclose _pclose
#else
  #include <unistd.h>
#endif

typedef enum { BLK_ENV, BLK_MATH, BLK_PYTHON, BLK_RAW } BlockKind;
typedef enum { PYRES_VERBATIM, PYRES_TEX } PyResultsMode;

typedef struct { char *data; size_t len; size_t cap; } StrBuf;

static void die(const char *msg){ fprintf(stderr,"easylatex: %s\n",msg); exit(1); }
static void *xmalloc(size_t n){ void *p=malloc(n); if(!p) die("out of memory"); return p; }
static void *xrealloc(void *p,size_t n){ void *q=realloc(p,n); if(!q) die("out of memory"); return q; }
static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=(char*)xmalloc(n); memcpy(p,s,n); return p; }

static void sb_init(StrBuf *sb){ sb->data=NULL; sb->len=0; sb->cap=0; }
static void sb_free(StrBuf *sb){ free(sb->data); sb->data=NULL; sb->len=sb->cap=0; }
static void sb_reserve(StrBuf *sb,size_t need){
  if(need<=sb->cap) return;
  size_t cap=sb->cap?sb->cap:256;
  while(cap<need) cap*=2;
  sb->data=(char*)realloc(sb->data,cap);
  if(!sb->data) die("out of memory");
  sb->cap=cap;
}
static void sb_append_n(StrBuf *sb,const char *s,size_t n){
  sb_reserve(sb,sb->len+n+1);
  memcpy(sb->data+sb->len,s,n);
  sb->len+=n;
  sb->data[sb->len]='\0';
}
static void sb_append(StrBuf *sb,const char *s){ sb_append_n(sb,s,strlen(s)); }
static void sb_append_char(StrBuf *sb,char c){ sb_append_n(sb,&c,1); }

typedef struct {
  BlockKind kind;
  int indent_cols;

  char *env_name;
  bool is_list;

  int  math_base_cols;
  char *math_pending;
  bool math_raw_sticky;

  int py_base_cols;
  PyResultsMode py_mode;
  StrBuf py_code;

  int raw_base_cols;
} Block;

typedef struct { Block *data; size_t len; size_t cap; } BlockStack;

static void stack_init(BlockStack *st){ st->data=NULL; st->len=0; st->cap=0; }
static void stack_free(BlockStack *st){ free(st->data); st->data=NULL; st->len=st->cap=0; }
static void stack_push(BlockStack *st, Block b){
  if(st->len==st->cap){
    st->cap=st->cap?st->cap*2:16;
    st->data=(Block*)xrealloc(st->data, st->cap*sizeof(Block));
  }
  st->data[st->len++]=b;
}
static Block *stack_top(BlockStack *st){ return st->len?&st->data[st->len-1]:NULL; }
static Block stack_pop(BlockStack *st){ if(!st->len) die("internal: pop empty"); return st->data[--st->len]; }

static void rstrip_inplace(char *s){
  size_t n=strlen(s);
  while(n>0 && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n]='\0';
}
static const char *lskip_spaces(const char *s){ while(*s==' '||*s=='\t') s++; return s; }
static bool is_blank_line(const char *s){ while(*s){ if(!isspace((unsigned char)*s)) return false; s++; } return true; }

static int calc_indent_cols(const char *line, int *consumed_bytes){
  int cols=0,i=0;
  while(line[i]==' '||line[i]=='\t'){ cols += (line[i]=='\t')?TAB_WIDTH:1; i++; }
  if(consumed_bytes) *consumed_bytes=i;
  return cols;
}
static char *read_line(FILE *fp){
  size_t cap=256,len=0;
  char *buf=(char*)xmalloc(cap);
  for(;;){
    if(!fgets(buf+len,(int)(cap-len),fp)){
      if(len==0){ free(buf); return NULL; }
      return buf;
    }
    len+=strlen(buf+len);
    if(len && buf[len-1]=='\n') return buf;
    cap*=2;
    buf=(char*)xrealloc(buf,cap);
  }
}
static const char *strip_cols(const char *line,int cols_to_strip){
  int cols=0,i=0;
  while(line[i]==' '||line[i]=='\t'){
    int add=(line[i]=='\t')?TAB_WIDTH:1;
    if(cols+add>cols_to_strip) break;
    cols+=add; i++;
    if(cols==cols_to_strip) break;
  }
  return line+i;
}

static bool streq(const char *a, const char *b){ return strcmp(a,b)==0; }

static bool is_list_env_name(const char *env){
  return streq(env,"itemize") || streq(env,"enumerate") || streq(env,"description");
}
static bool inside_list_env(const BlockStack *st){
  if(!st->len) return false;
  const Block *top=&st->data[st->len-1];
  return top->kind==BLK_ENV && top->is_list;
}
static const char *strip_list_marker(const char *s){
  s=lskip_spaces(s);
  if((s[0]=='-'||s[0]=='*') && s[1]==' ') return s+2;
  return s;
}

static bool looks_like_command_call(const char *content){
  const char *s=content;
  if(!(*s=='_'||isalpha((unsigned char)*s))) return false;
  s++;
  while(*s=='_'||isalnum((unsigned char)*s)) s++;
  return (*s=='{'||*s=='[');
}

static bool is_title_command(const char *name){
  return streq(name,"part") ||
         streq(name,"chapter") ||
         streq(name,"section") ||
         streq(name,"subsection") ||
         streq(name,"subsubsection") ||
         streq(name,"paragraph") ||
         streq(name,"subparagraph") ||
         streq(name,"frametitle") ||
         streq(name,"framesubtitle");
}

static bool is_braced_command(const char *name){
  return streq(name,"title") ||
         streq(name,"subtitle") ||
         streq(name,"author") ||
         streq(name,"institute") ||
         streq(name,"date") ||
         streq(name,"caption") ||
         streq(name,"label") ||
         streq(name,"ref") ||
         streq(name,"pageref") ||
         streq(name,"nameref") ||
         streq(name,"eqref") ||
         streq(name,"url") ||
         streq(name,"href") ||
         streq(name,"emph") ||
         streq(name,"textbf") ||
         streq(name,"textit") ||
         streq(name,"texttt") ||
         streq(name,"textsc") ||
         streq(name,"underline") ||
         streq(name,"textrm") ||
         streq(name,"textsf") ||
         streq(name,"textmd") ||
         streq(name,"textup") ||
         streq(name,"textsl") ||
         streq(name,"textnormal") ||
         streq(name,"textsuperscript") ||
         streq(name,"textsubscript") ||
         streq(name,"input") ||
         streq(name,"include") ||
         streq(name,"includegraphics");
}

static bool is_nobody_command(const char *name){
  return streq(name,"tableofcontents") ||
         streq(name,"listoffigures") ||
         streq(name,"listoftables") ||
         streq(name,"maketitle") ||
         streq(name,"newpage") ||
         streq(name,"clearpage") ||
         streq(name,"cleardoublepage") ||
         streq(name,"smallskip") ||
         streq(name,"medskip") ||
         streq(name,"bigskip") ||
         streq(name,"linebreak") ||
         streq(name,"pagebreak") ||
         streq(name,"nolinebreak") ||
         streq(name,"nopagebreak") ||
         streq(name,"pause") ||
         streq(name,"centering") ||
         streq(name,"raggedright") ||
         streq(name,"raggedleft");
}

static bool is_known_environment(const char *name){
  return
    streq(name,"center") ||
    streq(name,"flushleft") ||
    streq(name,"flushright") ||
    streq(name,"quote") ||
    streq(name,"quotation") ||
    streq(name,"verse") ||
    streq(name,"abstract") ||
    streq(name,"titlepage") ||
    streq(name,"itemize") ||
    streq(name,"enumerate") ||
    streq(name,"description") ||
    streq(name,"figure") ||
    streq(name,"figure*") ||
    streq(name,"table") ||
    streq(name,"table*") ||
    streq(name,"tabular") ||
    streq(name,"tabular*") ||
    streq(name,"tabularx") ||
    streq(name,"longtable") ||
    streq(name,"equation") ||
    streq(name,"equation*") ||
    streq(name,"align") ||
    streq(name,"align*") ||
    streq(name,"gather") ||
    streq(name,"gather*") ||
    streq(name,"multline") ||
    streq(name,"multline*") ||
    streq(name,"flalign") ||
    streq(name,"flalign*") ||
    streq(name,"split") ||
    streq(name,"cases") ||
    streq(name,"theorem") ||
    streq(name,"lemma") ||
    streq(name,"proposition") ||
    streq(name,"corollary") ||
    streq(name,"claim") ||
    streq(name,"definition") ||
    streq(name,"example") ||
    streq(name,"remark") ||
    streq(name,"proof") ||
    streq(name,"thebibliography") ||
    streq(name,"minipage") ||
    streq(name,"verbatim") ||
    streq(name,"lstlisting");
}

static bool is_recognized_header_name(const char *name){
  return streq(name,"latex") || streq(name,"math") || streq(name,"python") ||
         is_title_command(name) || is_braced_command(name) || is_nobody_command(name) ||
         is_known_environment(name);
}

static bool parse_header(const char *content_in,
                         char **name_out,
                         char **args_before_out,
                         char **inline_after_out)
{
  char *tmp = xstrdup(content_in);
  rstrip_inplace(tmp);

  const char *s0 = lskip_spaces(tmp);
  if (*s0 == '\0') { free(tmp); return false; }

  if (!(*s0=='_' || isalpha((unsigned char)*s0))) { free(tmp); return false; }
  const char *p = s0 + 1;
  while (*p=='_'||isalnum((unsigned char)*p)) p++;

  size_t name_len = (size_t)(p - s0);
  char *name = (char*)xmalloc(name_len + 1);
  memcpy(name, s0, name_len);
  name[name_len] = '\0';

  const char *scan = p;

  while (*scan==' ' || *scan=='\t') scan++;

  for (;;) {
    if (*scan == '[') {
      int depth = 1;
      scan++;
      while (*scan && depth > 0) {
        if (*scan == '[') depth++;
        else if (*scan == ']') depth--;
        scan++;
      }
      if (depth != 0) { free(name); free(tmp); return false; }
      while (*scan==' ' || *scan=='\t') scan++;
      continue;
    }
    if (*scan == '{') {
      int depth = 1;
      scan++;
      while (*scan && depth > 0) {
        if (*scan == '{') depth++;
        else if (*scan == '}') depth--;
        scan++;
      }
      if (depth != 0) { free(name); free(tmp); return false; }
      while (*scan==' ' || *scan=='\t') scan++;
      continue;
    }
    break;
  }

  if (*scan != ':') {
    free(name);
    free(tmp);
    return false;
  }

  const char *colon = scan;

  size_t args_len = (size_t)(colon - p);
  char *args_raw = (char*)xmalloc(args_len + 1);
  memcpy(args_raw, p, args_len);
  args_raw[args_len] = '\0';
  rstrip_inplace(args_raw);
  const char *args_trim = lskip_spaces(args_raw);
  char *args = xstrdup(args_trim);
  free(args_raw);

  const char *after = colon + 1;
  after = lskip_spaces(after);
  char *inline_after = xstrdup(after);

  *name_out = name;
  *args_before_out = args;
  *inline_after_out = inline_after;

  free(tmp);
  return true;
}

static PyResultsMode parse_python_results_mode(const char *args_before){
  const char *lb = strchr(args_before,'[');
  if(!lb) return PYRES_VERBATIM;
  const char *rb = strchr(lb+1,']');
  if(!rb) return PYRES_VERBATIM;

  size_t n=(size_t)(rb-(lb+1));
  char *opt=(char*)xmalloc(n+1);
  memcpy(opt,lb+1,n); opt[n]='\0';
  for(size_t i=0;i<n;i++) opt[i]=(char)tolower((unsigned char)opt[i]);

  PyResultsMode mode=PYRES_VERBATIM;
  if(strstr(opt,"results=tex")||strstr(opt,"results=asis")||strstr(opt,"results=raw")) mode=PYRES_TEX;
  free(opt);
  return mode;
}

static char *run_python_and_capture(const char *code){
  char tmp_path[512];

#ifdef _WIN32
  char *tn=tmpnam(NULL);
  if(!tn) die("tmpnam failed");
  snprintf(tmp_path,sizeof(tmp_path),"%s.py",tn);
  FILE *f=fopen(tmp_path,"wb");
  if(!f) die("failed to create temp python file");
#else
  char pattern[]="/tmp/easylatex_py_XXXXXX";
  int fd=mkstemp(pattern);
  if(fd<0) die("mkstemp failed");
  snprintf(tmp_path,sizeof(tmp_path),"%s.py",pattern);
  FILE *f=fopen(tmp_path,"wb");
  if(!f){ close(fd); die("failed to open temp python file"); }
  close(fd);
#endif

  fwrite(code,1,strlen(code),f);
  fclose(f);

  char cmd[1024];
  FILE *pipe=NULL;
  snprintf(cmd,sizeof(cmd),"python3 \"%s\" 2>&1",tmp_path);
  pipe=popen(cmd,"r");
  if(!pipe){
    snprintf(cmd,sizeof(cmd),"python \"%s\" 2>&1",tmp_path);
    pipe=popen(cmd,"r");
  }

  StrBuf out; sb_init(&out);
  if(!pipe){
    sb_append(&out,"ERROR: could not run python (python3/python not found)\n");
  } else {
    char buf[4096];
    while(fgets(buf,(int)sizeof(buf),pipe)) sb_append(&out,buf);
    pclose(pipe);
  }

  remove(tmp_path);
  return out.data ? out.data : xstrdup("");
}

static bool g_doc_open=false;

static void emit_default_preamble_once(void){
  if(g_doc_open) return;

  fputs("\\documentclass{article}\n", stdout);
  fputs("\\usepackage[T1]{fontenc}\n", stdout);
  fputs("\\usepackage[utf8]{inputenc}\n", stdout);

  fputs("\\usepackage{amsmath}\n", stdout);
  fputs("\\usepackage{amssymb}\n", stdout);
  fputs("\\usepackage{amsthm}\n", stdout);

  fputs("\\usepackage{graphicx}\n", stdout);
  fputs("\\usepackage{hyperref}\n", stdout);

  fputs("\\usepackage{booktabs}\n", stdout);
  fputs("\\usepackage{tabularx}\n", stdout);
  fputs("\\usepackage{longtable}\n", stdout);
  fputs("\\usepackage{xcolor}\n", stdout);
  fputs("\\usepackage{listings}\n", stdout);

  fputs("\\theoremstyle{plain}\n", stdout);
  fputs("\\newtheorem{theorem}{Theorem}[section]\n", stdout);
  fputs("\\newtheorem{lemma}[theorem]{Lemma}\n", stdout);
  fputs("\\newtheorem{proposition}[theorem]{Proposition}\n", stdout);
  fputs("\\newtheorem{corollary}[theorem]{Corollary}\n", stdout);
  fputs("\\newtheorem{claim}[theorem]{Claim}\n", stdout);
  fputs("\\theoremstyle{definition}\n", stdout);
  fputs("\\newtheorem{definition}[theorem]{Definition}\n", stdout);
  fputs("\\newtheorem{example}[theorem]{Example}\n", stdout);
  fputs("\\theoremstyle{remark}\n", stdout);
  fputs("\\newtheorem{remark}[theorem]{Remark}\n", stdout);

  fputs("\\begin{document}\n", stdout);
  g_doc_open=true;
}

static void emit_end_document_if_needed(void){
  if(g_doc_open){
    fputs("\\end{document}\n", stdout);
    g_doc_open=false;
  }
}

static void fputs_with_n_escapes_inline(const char *s){
  for(size_t i=0; s[i]; ){
    if(s[i]=='\\' && s[i+1]=='n'){
      fputs("\\\\", stdout);
      i += 2;
    } else {
      fputc(s[i], stdout);
      i++;
    }
  }
}

static void emit_text_with_n_escapes(const char *s){
  emit_default_preamble_once();
  for(size_t i=0;s[i];){
    if(s[i]=='\\' && s[i+1]=='n'){ fputs("\\\\\n", stdout); i+=2; }
    else { fputc(s[i], stdout); i++; }
  }
  fputc('\n', stdout);
}

static void math_flush_pending(Block *m){
  if(m->math_pending){
    fputs(m->math_pending, stdout);
    fputc('\n', stdout);
    free(m->math_pending);
    m->math_pending=NULL;
  }
}
static void math_blank_line(Block *m){
  if(m->math_pending){
    fputs(m->math_pending, stdout);
    fputs(" \\\\[0.6em]\n", stdout);
    free(m->math_pending);
    m->math_pending=NULL;
  }
}
static void math_feed_row(Block *m, const char *row_text){
  const char *p=row_text;
  while(*p){
    const char *q=p;
    while(*q && !(q[0]=='\\' && q[1]=='n')) q++;

    size_t part_len=(size_t)(q-p);
    char *part=(char*)xmalloc(part_len+1);
    memcpy(part,p,part_len); part[part_len]='\0';

    if(m->math_pending){
      fputs(m->math_pending, stdout);
      fputs(" \\\\\n", stdout);
      free(m->math_pending);
    }
    m->math_pending=part;

    if(*q=='\0') break;
    p=q+2;
  }
}

static void close_one_block(BlockStack *st){
  Block b=stack_pop(st);

  if(b.kind==BLK_ENV){
    emit_default_preamble_once();
    fprintf(stdout,"\\end{%s}\n", b.env_name);
    free(b.env_name);
    return;
  }
  if(b.kind==BLK_RAW){
    return;
  }
  if(b.kind==BLK_MATH){
    math_flush_pending(&b);
    fputs("\\end{aligned}\n\\]\n", stdout);
    return;
  }
  if(b.kind==BLK_PYTHON){
    const char *code=b.py_code.data?b.py_code.data:"";
    char *out=run_python_and_capture(code);

    emit_default_preamble_once();
    if(b.py_mode==PYRES_TEX){
      fputs(out, stdout);
      if(out[0] && out[strlen(out)-1] != '\n') fputc('\n', stdout);
    } else {
      fputs("\\begin{verbatim}\n", stdout);
      fputs(out, stdout);
      if(out[0] && out[strlen(out)-1] != '\n') fputc('\n', stdout);
      fputs("\\end{verbatim}\n", stdout);
    }

    free(out);
    sb_free(&b.py_code);
    return;
  }
}

static void close_blocks_for_indent(BlockStack *st, int indent_cols){
  for(;;){
    Block *top=stack_top(st);
    if(!top) break;
    if(indent_cols <= top->indent_cols) close_one_block(st);
    else break;
  }
}

int main(int argc, char **argv){
  FILE *fp=stdin;
  if(argc>=2){
    fp=fopen(argv[1],"rb");
    if(!fp){ fprintf(stderr,"easylatex: cannot open %s\n", argv[1]); return 1; }
  }

  BlockStack st; stack_init(&st);

  char *pending_line = NULL;

  for(;;){
    char *line=NULL;
    if(pending_line){
      line=pending_line;
      pending_line=NULL;
    } else {
      line=read_line(fp);
    }
    if(!line) break;

    rstrip_inplace(line);

    int consumed=0;
    int indent_cols=calc_indent_cols(line,&consumed);
    const char *content=line+consumed;

    Block *t0=stack_top(&st);
    if(is_blank_line(content)){
      if(t0 && t0->kind==BLK_MATH) math_blank_line(t0);
      else fputc('\n', stdout);
      free(line);
      continue;
    }

    close_blocks_for_indent(&st, indent_cols);
    Block *top=stack_top(&st);

    if(top && top->kind==BLK_RAW){
      if(top->raw_base_cols<0) top->raw_base_cols=indent_cols;
      const char *s=strip_cols(line, top->raw_base_cols);
      emit_default_preamble_once();
      fputs(s, stdout);
      fputc('\n', stdout);
      free(line);
      continue;
    }

    if(top && top->kind==BLK_MATH){
      if(top->math_base_cols<0) top->math_base_cols=indent_cols;
      const char *s=strip_cols(line, top->math_base_cols);
      s=lskip_spaces(s);

      if(strcmp(s,"latex:")==0){
        top->math_raw_sticky=true;
        free(line);
        continue;
      }

      math_feed_row(top, s);

      free(line);
      continue;
    }

    if(top && top->kind==BLK_PYTHON){
      if(top->py_base_cols<0) top->py_base_cols=indent_cols;
      const char *s=strip_cols(line, top->py_base_cols);
      sb_append(&top->py_code, s);
      sb_append_char(&top->py_code, '\n');
      free(line);
      continue;
    }

    char *name=NULL, *args_before=NULL, *inline_after=NULL;
    if(parse_header(content, &name, &args_before, &inline_after)){

      if(!is_recognized_header_name(name)){
        emit_text_with_n_escapes(content);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(is_nobody_command(name)){
        emit_default_preamble_once();
        fprintf(stdout, "\\%s\n", name);

        for(;;){
          char *nxt = read_line(fp);
          if(!nxt) break;

          rstrip_inplace(nxt);
          int c2=0;
          int ind2=calc_indent_cols(nxt, &c2);
          const char *ct2 = nxt + c2;

          if(is_blank_line(ct2)){ free(nxt); continue; }

          if(ind2 > indent_cols){
            free(nxt);
            continue;
          }

          pending_line = nxt;
          break;
        }

        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(is_braced_command(name)){
        emit_default_preamble_once();

        if(args_before[0] != '\0'){
          fprintf(stdout, "\\%s%s\n", name, args_before);

          for(;;){
            char *nxt = read_line(fp);
            if(!nxt) break;

            rstrip_inplace(nxt);
            int c2=0;
            int ind2=calc_indent_cols(nxt, &c2);
            const char *ct2 = nxt + c2;

            if(is_blank_line(ct2)){ free(nxt); continue; }
            if(ind2 > indent_cols){ free(nxt); continue; }

            pending_line = nxt;
            break;
          }

          free(name); free(args_before); free(inline_after); free(line);
          continue;
        }

        StrBuf body; sb_init(&body);

        if(inline_after[0] != '\0'){
          sb_append(&body, inline_after);
        }

        for(;;){
          char *nxt = read_line(fp);
          if(!nxt) break;

          rstrip_inplace(nxt);
          int c2=0;
          int ind2=calc_indent_cols(nxt, &c2);
          const char *ct2 = nxt + c2;

          if(is_blank_line(ct2)){ free(nxt); continue; }

          if(ind2 <= indent_cols){
            pending_line = nxt;
            break;
          }

          const char *t = lskip_spaces(ct2);
          if(body.len > 0) sb_append(&body, " \\\\ ");
          sb_append(&body, t);
          free(nxt);
        }

        fprintf(stdout, "\\%s{", name);
        if(body.data) fputs_with_n_escapes_inline(body.data);
        fprintf(stdout, "}\n");

        sb_free(&body);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(is_title_command(name)){
        emit_default_preamble_once();

        if(args_before[0] != '\0'){
          fprintf(stdout, "\\%s%s\n", name, args_before);

          for(;;){
            char *nxt = read_line(fp);
            if(!nxt) break;

            rstrip_inplace(nxt);
            int c2=0;
            int ind2=calc_indent_cols(nxt, &c2);
            const char *ct2 = nxt + c2;

            if(is_blank_line(ct2)){ free(nxt); continue; }
            if(ind2 > indent_cols){ free(nxt); continue; }

            pending_line = nxt;
            break;
          }

          free(name); free(args_before); free(inline_after); free(line);
          continue;
        }

        char *title = NULL;

        if(inline_after[0] != '\0'){
          title = xstrdup(inline_after);
        } else {
          for(;;){
            char *nxt = read_line(fp);
            if(!nxt) break;

            rstrip_inplace(nxt);
            int c2=0;
            int ind2=calc_indent_cols(nxt, &c2);
            const char *ct2 = nxt + c2;

            if(is_blank_line(ct2)){ free(nxt); continue; }

            if(ind2 <= indent_cols){
              pending_line = nxt;
              break;
            }

            title = xstrdup(lskip_spaces(ct2));
            free(nxt);
            break;
          }
        }

        if(!title) title = xstrdup("");

        fprintf(stdout, "\\%s{", name);
        fputs_with_n_escapes_inline(title);
        fprintf(stdout, "}\n");

        free(title);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(streq(name,"latex")){
        Block b={0};
        b.kind=BLK_RAW;
        b.indent_cols=indent_cols;
        b.raw_base_cols=-1;
        stack_push(&st,b);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(streq(name,"math")){
        emit_default_preamble_once();
        fputs("\\[\n\\begin{aligned}\n", stdout);
        Block b={0};
        b.kind=BLK_MATH;
        b.indent_cols=indent_cols;
        b.math_base_cols=-1;
        b.math_pending=NULL;
        b.math_raw_sticky=false;
        stack_push(&st,b);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(streq(name,"python")){
        Block b={0};
        b.kind=BLK_PYTHON;
        b.indent_cols=indent_cols;
        b.py_base_cols=-1;
        b.py_mode=parse_python_results_mode(args_before);
        sb_init(&b.py_code);
        stack_push(&st,b);
        free(name); free(args_before); free(inline_after); free(line);
        continue;
      }

      if(is_known_environment(name)){
        emit_default_preamble_once();
        fprintf(stdout,"\\begin{%s}%s\n", name, args_before);

        Block b={0};
        b.kind=BLK_ENV;
        b.indent_cols=indent_cols;
        b.env_name=name;
        b.is_list=is_list_env_name(name);
        stack_push(&st,b);

        if(inline_after[0] != '\0'){
          emit_text_with_n_escapes(inline_after);
        }

        free(args_before);
        free(inline_after);
        free(line);
        continue;
      }

      emit_text_with_n_escapes(content);
      free(name); free(args_before); free(inline_after); free(line);
      continue;
    }

    if(content[0]=='\\'){
      emit_default_preamble_once();
      fputs(content, stdout);
      fputc('\n', stdout);
      free(line);
      continue;
    }

    if(looks_like_command_call(content)){
      emit_default_preamble_once();
      fputc('\\', stdout);
      fputs(content, stdout);
      fputc('\n', stdout);
      free(line);
      continue;
    }

    if(inside_list_env(&st)){
      emit_default_preamble_once();
      const char *item=strip_list_marker(content);
      fputs("\\item ", stdout);
      for(size_t i=0; item[i];){
        if(item[i]=='\\' && item[i+1]=='n'){ fputs("\\\\\n", stdout); i+=2; }
        else { fputc(item[i], stdout); i++; }
      }
      fputc('\n', stdout);
      free(line);
      continue;
    }

    emit_text_with_n_escapes(content);
    free(line);
  }

  while(st.len>0) close_one_block(&st);
  stack_free(&st);

  if(pending_line) free(pending_line);

  emit_end_document_if_needed();

  if(fp!=stdin) fclose(fp);
  return 0;
}