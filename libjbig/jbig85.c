/*
 *  T.85 "light" version of the portable JBIG image compression library
 *
 *  Copyright 1995-2007 -- Markus Kuhn -- http://www.cl.cam.ac.uk/~mgk25/
 *
 *  $Id$
 *
 *  This module implements a portable standard C encoder and decoder
 *  using the JBIG1 lossless bi-level image compression algorithm
 *  specified in International Standard ISO 11544:1993 and
 *  ITU-T Recommendation T.82. See the file jbig.txt for usage
 *  instructions and application examples.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 *  If you want to use this program under different license conditions,
 *  then contact the author for an arrangement.
 *
 *  It is possible that certain products which can be built using this
 *  software module might form inventions protected by patent rights in
 *  some countries (e.g., by patents about arithmetic coding algorithms
 *  owned by IBM and AT&T in the USA). Provision of this software by the
 *  author does NOT include any licences for any patents. In those
 *  countries where a patent licence is required for certain applications
 *  of this software module, you will have to obtain such a licence
 *  yourself.
 */

#ifdef DEBUG
#include <stdio.h>
#else
#define NDEBUG
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "jbig85.h"

#define TPB2CX  0x195  /* contexts for TP special pixels */
#define TPB3CX  0x0e5

/* marker codes */
#define MARKER_STUFF    0x00
#define MARKER_RESERVE  0x01
#define MARKER_SDNORM   0x02
#define MARKER_SDRST    0x03
#define MARKER_ABORT    0x04
#define MARKER_NEWLEN   0x05
#define MARKER_ATMOVE   0x06
#define MARKER_COMMENT  0x07
#define MARKER_ESC      0xff

/* object code version id */

const char jbg85_version[] = 
  "JBIG-KIT " JBG85_VERSION " (T.85 version) -- (c) 1995-2008 Markus Kuhn -- "
  "Licence: " JBG85_LICENCE "\n"
  "$Id$\n";

#define _(String) String  /* to mark translatable string for GNU gettext */

/*
 * Array with English ASCII error messages that correspond
 * to return values from public functions in this library.
 */
static const char *errmsg[] = {
  _("Everything is OK"),                                     /* JBG_EOK */
  _("Reached specified maximum image size"),                 /* JBG_EOK_INTR */
  _("Unexpected end of input data stream"),                  /* JBG_EAGAIN */
  _("Not enough memory available"),                          /* JBG_ENOMEM */
  _("ABORT marker segment encountered"),                     /* JBG_EABORT */
  _("Unknown marker segment encountered"),                   /* JBG_EMARKER */
  _("Incremental BIE does not continue previous one"),       /* JBG_ENOCONT */
  _("Input data stream contains invalid data"),              /* JBG_EINVAL */
  _("Input data stream uses unimplemented JBIG features")    /* JBG_EIMPL */
};


/*
 * Callback adapter function for arithmetic encoder
 */
static void enc_byte_out(int byte, void *s)
{
  unsigned char c = byte;
  ((struct jbg85_enc_state *)s)->data_out(&c, sizeof(unsigned char),
					  ((struct jbg85_enc_state *)s)->file);
}


/*
 * Initialize the status struct for the encoder.
 */
void jbg85_enc_init(struct jbg85_enc_state *s,
		    unsigned long x0, unsigned long y0,
		    void (*data_out)(unsigned char *start, size_t len,
				     void *file),
		    void *file)
{
  assert(x0 > 0 && y0 > 0);
  s->x0 = x0;
  s->y0 = y0;
  s->newlen = 0;       /* no NEWLEN pending or output */
  s->data_out = data_out;
  s->file = file;

  s->l0 = s->y0 / 35;             /* 35 stripes/image suggested default */
  if (s->l0 > 128) s->l0 = 128;
  else if (s->l0 < 2) s->l0 = 2;
#if 1
  s->l0 = 128; /* T.85 BASIC setting */
#endif
  s->mx = 127;
  s->new_tx = -1;                /* no ATMOVE pending */
  s->tx = 0;
  s->options = JBG_TPBON | JBG_VLENGTH;
  s->comment = NULL;            /* no COMMENT pending */
  s->y = 0;
  s->i = 0;
  s->ltp_old = 0;
  
  /* initialize arithmetic encoder */
  arith_encode_init(&s->s, 0);
  s->s.byte_out = &enc_byte_out;
  s->s.file = s;
  
  return;
}


/*
 * The following function allows to specify the bits describing the
 * options of the format as well as the maximum AT movement window and
 * the number of layer 0 lines per stripes.
 */
void jbg85_enc_options(struct jbg85_enc_state *s, int options,
		     unsigned long l0, int mx)
{
  if (options >= 0) s->options = options;
  if (l0 > 0) s->l0 = l0;
  if (mx >= 0 && mx < 128) s->mx = mx;

  return;
}


/* auxiliary routine to write out NEWLEN */
static void output_newlen(struct jbg85_enc_state *s)
{
  unsigned char buf[6];

  assert(s->i == 0);
  if (s->newlen != 1)
    return;
  buf[0] = MARKER_ESC;
  buf[1] = MARKER_NEWLEN;
  buf[2] =  s->y0 >> 24;
  buf[3] = (s->y0 >> 16) & 0xff;
  buf[4] = (s->y0 >>  8) & 0xff;
  buf[5] =  s->y0        & 0xff;
  s->data_out(buf, 6, s->file);
  s->newlen = 2;
  if (s->y == s->y0) {
    /* if newlen refers to a line in the preceeding stripe, ITU-T T.82
     * section 6.2.6.2 requires us to append another SDNORM */
    buf[1] = MARKER_SDNORM;
    s->data_out(buf, 2, s->file);
  }
}


/*
 * Encode one full BIE and pass the generated data to the specified
 * call-back function
 */
void jbg85_enc_lineout(struct jbg85_enc_state *s, unsigned char *line,
		       unsigned char *prevline, unsigned char *prevprevline)
{
  unsigned char buf[20];
  unsigned long bpl;
  unsigned char *hp1, *hp2, *hp3, *p1, *q1;
  unsigned long line_h1 = 0, line_h2, line_h3;
  unsigned long j;  /* loop variable for pixel column */
  long o;
  unsigned a, p, t;
  int ltp;
  unsigned long cmin, cmax, clmin, clmax;
  int tmax;
#ifdef DEBUG
  static long tp_lines;
  static long encoded_pixels;
#endif

  if (s->y >= s->y0) {
    /* we have already output the full image, go away */
    return;
  }
  
  /* line 0 have no previous line */
  if (s->y < 1)
    prevline = NULL;
  if (s->y < 2)
    prevprevline = NULL;

  /* things that need to be done before the first line is encoded */
  if (s->y == 0) {
    /* prepare BIH */
    buf[0]  = 0;   /* DL = initial layer to be transmitted */
    buf[1]  = 0;   /* D  = number of differential layers */
    buf[2]  = 1;   /* P  = number of bit planes */
    buf[3]  = 0;
    buf[4]  =  s->x0 >> 24;
    buf[5]  = (s->x0 >> 16) & 0xff;
    buf[6]  = (s->x0 >>  8) & 0xff;
    buf[7]  =  s->x0        & 0xff;
    buf[8]  =  s->y0 >> 24;
    buf[9]  = (s->y0 >> 16) & 0xff;
    buf[10] = (s->y0 >>  8) & 0xff;
    buf[11] =  s->y0 & 0xff;
    buf[12] =  s->l0 >> 24;
    buf[13] = (s->l0 >> 16) & 0xff;
    buf[14] = (s->l0 >>  8) & 0xff;
    buf[15] =  s->l0 & 0xff;
    buf[16] = s->mx;
    buf[17] = 0;   /* MY = maximum vertical offset allowed for AT pixel */
    buf[18] = 0;   /* order: HITOLO = SEQ = ILEAVE = SMID = 0 */
    buf[19] = s->options & (JBG_LRLTWO | JBG_VLENGTH | JBG_TPBON);

    /* output BIH */
    s->data_out(buf, 20, s->file);
  }

  /* things that need to be done before the next SDE is encoded */
  if (s->i == 0) {

    /* output NEWLEN if there is any pending */
    output_newlen(s);

    /* output comment marker segment if there is any pending */
    if (s->comment) {
      buf[0] = MARKER_ESC;
      buf[1] = MARKER_COMMENT;
      buf[2] =  s->comment_len >> 24;
      buf[3] = (s->comment_len >> 16) & 0xff;
      buf[4] = (s->comment_len >>  8) & 0xff;
      buf[5] =  s->comment_len & 0xff;
      s->data_out(buf, 6, s->file);
      s->data_out(s->comment, s->comment_len, s->file);
      s->comment = NULL;
    }

    /* output ATMOVE if there is any pending */
    if (s->new_tx != -1 && s->new_tx != s->tx) {
      s->tx = s->new_tx;
      buf[0] = MARKER_ESC;
      buf[1] = MARKER_ATMOVE;
      buf[2] = 0;
      buf[3] = 0;
      buf[4] = 0;
      buf[5] = 0;
      buf[6] = s->tx;
      buf[7] = 0;
      s->data_out(buf, 8, s->file);
    }
    
    /* initialize adaptive template movement algorithm */
    if (s->mx == 0) {
      s->new_tx = 0;  /* ATMOVE has been disabled */
    } else {
      s->c_all = 0;
      for (t = 0; t <= s->mx; t++)
	s->c[t] = 0;
      s->new_tx = -1; /* we have yet to determine ATMOVE ... */
    }

    /* restart arithmetic encoder */
    arith_encode_init(&s->s, 1);
  }

#ifdef DEBUG
  if (s->y == 0)
    tp_lines = encoded_pixels = 0;
  fprintf(stderr, "encode line %lu (%2lu of stripe)\n", s->y, s->i);
#endif

  /* bytes per line */
  bpl = (s->x0 >> 3) + !!(s->x0 & 7);
  /* ensure correct zero padding of bitmap at the final byte of each line */
  if (s->x0 & 7) {
    line[bpl - 1] &= ~((1 << (8 - (s->x0 & 7))) - 1);
  }

  /* typical prediction */
  ltp = 0;
  if (s->options & JBG_TPBON) {
    p1 = line;
    q1 = prevline;
    ltp = 1;
    if (q1)
      while (p1 < line + bpl && (ltp = (*p1++ == *q1++)) != 0);
    else
      while (p1 < line + bpl && (ltp = (*p1++ == 0    )) != 0);
    arith_encode(&s->s, (s->options & JBG_LRLTWO) ? TPB2CX : TPB3CX,
		 ltp == s->ltp_old);
#ifdef DEBUG
    tp_lines += ltp;
#endif
    s->ltp_old = ltp;
  }
  
  if (!ltp) {

    /*
     * Layout of the variables line_h1, line_h2, line_h3, which contain
     * as bits the neighbour pixels of the currently coded pixel X:
     *
     *          76543210765432107654321076543210     line_h3
     *          76543210765432107654321076543210     line_h2
     *  76543210765432107654321X76543210             line_h1
     */
  
    /* pointer to first image byte of the three lines of interest */
    hp3 = prevprevline;
    hp2 = prevline;
    hp1 = line;
  
    line_h1 = line_h2 = line_h3 = 0;
    if (hp2) line_h2 = (long)*hp2 << 8;
    if (hp3) line_h3 = (long)*hp3 << 8;
  
    /* encode line */
    for (j = 0; j < s->x0;) {
      line_h1 |= *hp1;
      if (j < bpl * 8 - 8 && hp2) {
	line_h2 |= *(hp2 + 1);
	if (hp3)
	  line_h3 |= *(hp3 + 1);
      }
      if (s->options & JBG_LRLTWO) {
	/* two line template */
	do {
	  line_h1 <<= 1;  line_h2 <<= 1;  line_h3 <<= 1;
	  if (s->tx) {
	    if ((unsigned) s->tx > j)
	      a = 0;
	    else {
	      o = (j - s->tx) - (j & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      a <<= 4;
	    }
	    assert(s->tx > 23 ||
		   a == ((line_h1 >> (4 + s->tx)) & 0x010));
	    arith_encode(&s->s, (((line_h2 >> 10) & 0x3e0) | a |
				 ((line_h1 >>  9) & 0x00f)),
			 (line_h1 >> 8) & 1);
	  }
	  else
	    arith_encode(&s->s, (((line_h2 >> 10) & 0x3f0) |
				 ((line_h1 >>  9) & 0x00f)),
			 (line_h1 >> 8) & 1);
#ifdef DEBUG
	  encoded_pixels++;
#endif
	  /* statistics for adaptive template changes */
	  if (s->new_tx == -1 && j >= s->mx && j < s->x0 - 2) {
	    p = (line_h1 & 0x100) != 0; /* current pixel value */
	    s->c[0] += ((line_h2 & 0x4000) != 0) == p; /* default position */
	    assert(!(((line_h2 >> 6) ^ line_h1) & 0x100) ==
		   (((line_h2 & 0x4000) != 0) == p));
	    for (t = 5; t <= s->mx && t <= j; t++) {
	      o = (j - t) - (j & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      assert(t > 23 ||
		     (a == p) == !(((line_h1 >> t) ^ line_h1) & 0x100));
	      s->c[t] += a == p;
	    }
	    for (; t <= s->mx; t++) {
	      s->c[t] += 0 == p;
	    }
	    ++s->c_all;
	  }
	} while (++j & 7 && j < s->x0);
      } else {
	/* three line template */
	do {
	  line_h1 <<= 1;  line_h2 <<= 1;  line_h3 <<= 1;
	  if (s->tx) {
	    if ((unsigned) s->tx > j)
	      a = 0;
	    else {
	      o = (j - s->tx) - (j & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      a <<= 2;
	    }
	    assert(s->tx > 23 ||
		   a == ((line_h1 >> (6 + s->tx)) & 0x004));
	    arith_encode(&s->s, (((line_h3 >>  8) & 0x380) |
				 ((line_h2 >> 12) & 0x078) | a |
				 ((line_h1 >>  9) & 0x003)),
			 (line_h1 >> 8) & 1);
	  } else
	    arith_encode(&s->s, (((line_h3 >>  8) & 0x380) |
				 ((line_h2 >> 12) & 0x07c) |
				 ((line_h1 >>  9) & 0x003)),
			 (line_h1 >> 8) & 1);
#ifdef DEBUG
	  encoded_pixels++;
#endif
	  /* statistics for adaptive template changes */
	  if (s->new_tx == -1 && j >= s->mx && j < s->x0 - 2) {
	    p = (line_h1 & 0x100) != 0; /* current pixel value */
	    s->c[0] += ((line_h2 & 0x4000) != 0) == p; /* default position */
	    assert(!(((line_h2 >> 6) ^ line_h1) & 0x100) ==
		   (((line_h2 & 0x4000) != 0) == p));
	    for (t = 3; t <= s->mx && t <= j; t++) {
	      o = (j - t) - (j & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      assert(t > 23 ||
		     (a == p) == !(((line_h1 >> t) ^ line_h1) & 0x100));
	      s->c[t] += a == p;
	    }
	    for (; t <= s->mx; t++) {
	      s->c[t] += 0 == p;
	    }
	    ++s->c_all;
	  }
	} while (++j & 7 && j < s->x0);
      } /* if (s->options & JBG_LRLTWO) */
      hp1++;
      if (hp2) hp2++;
      if (hp3) hp3++;
    } /* for (j = ...) */
  } /* if (!ltp) */

  /* line is complete now, deal with end of stripe */
  s->i++; s->y++;
  if (s->i == s->l0 || s->y == s->y0) {
    /* end of stripe reached */
    arith_encode_flush(&s->s);
    buf[0] = MARKER_ESC;
    buf[1] = MARKER_SDNORM;
    s->data_out(buf, 2, s->file);
    s->i = 0;

    /* output NEWLEN if there is any pending */
    output_newlen(s);
  }

  /* check whether it is worth to perform an ATMOVE */
  if (s->new_tx == -1 && s->c_all > 2048) {
    cmin = clmin = 0xffffffffL;
    cmax = clmax = 0;
    tmax = 0;
    for (t = (s->options & JBG_LRLTWO) ? 5 : 3; t <= s->mx; t++) {
      if (s->c[t] > cmax) cmax = s->c[t];
      if (s->c[t] < cmin) cmin = s->c[t];
      if (s->c[t] > s->c[tmax]) tmax = t;
    }
    clmin = (s->c[0] < cmin) ? s->c[0] : cmin;
    clmax = (s->c[0] > cmax) ? s->c[0] : cmax;
    if (s->c_all - cmax < (s->c_all >> 3) &&
	cmax - s->c[s->tx] > s->c_all - cmax &&
	cmax - s->c[s->tx] > (s->c_all >> 4) &&
	/*                 ^ T.82 said < here, fixed in Cor.1/25 */
	cmax - (s->c_all - s->c[s->tx]) > s->c_all - cmax &&
	cmax - (s->c_all - s->c[s->tx]) > (s->c_all >> 4) &&
	cmax - cmin > (s->c_all >> 2) &&
	(s->tx || clmax - clmin > (s->c_all >> 3))) {
      /* we have decided to perform an ATMOVE */
      s->new_tx = tmax;
#ifdef DEBUG
      fprintf(stderr, "ATMOVE: tx=%d, c_all=%d\n",
	      s->new_tx, s->c_all);
#endif
    } else {
      s->new_tx = s->tx;  /* we have decided not to perform an ATMOVE */
    }
  }
  assert(s->tx >= 0); /* i.e., tx can safely be cast to unsigned */
  
#ifdef DEBUG
  if (s->y == s->y0)
    fprintf(stderr, "tp_lines = %ld, encoded_pixels = %ld\n",
	    tp_lines, encoded_pixels);
#endif

  return;
}


/*
 * Inform encoder about new (reduced) height of image
 */
void jbg85_enc_newlen(struct jbg85_enc_state *s, unsigned long newlen)
{
  unsigned char buf[6];

  if (s->newlen == 2 || newlen >= s->y0 || newlen < 1 ||
      !(s->options & JBG_VLENGTH)) {
    /* invalid invocation or parameter */
    return;
  }
  if (newlen < s->y) {
    /* we are already beyond the new end, therefore move the new end */
    newlen = s->y;
  }
  if (s->y > 0 && s->y0 != newlen)
    s->newlen = 1;
  s->y0 = newlen;
  if (s->y == s->y0) {
    /* we are already at the end; abort the current stripe if necessary */
    if (s->i > 0) {
      arith_encode_flush(&s->s);
      buf[0] = MARKER_ESC;
      buf[1] = MARKER_SDNORM;
      s->data_out(buf, 2, s->file);
      s->i = 0;
    }
    /* output NEWLEN if there is any pending */
    output_newlen(s);
  }
}


/*
 * Convert the error codes used by jbg85_dec_in() into an English ASCII string
 */
const char *jbg85_strerror(int errnum)
{
  if (errnum < 0 || (unsigned) errnum >= sizeof(errmsg)/sizeof(errmsg[0]))
    return "Unknown error code passed to jbg85_strerror()";

  return errmsg[errnum];
}


/*
 * The constructor for a decoder 
 */
void jbg85_dec_init(struct jbg85_dec_state *s,
		    unsigned char *buf, size_t buflen,
		    void (*line_out)(unsigned char *start, size_t len,
				     unsigned long y, void *file),
		    void *file)
{
  s->ymax = 4294967295UL;
  s->linebuf = buf;
  s->linebuf_len = buflen;
  s->line_out = line_out;
  s->file = file;
  s->bie_len = 0;
  s->buf_len = 0;
  arith_decode_init(&s->s, 0);
  return;
}


/*
 * Specify a maximum image height for the decoder. It will abort to
 * decode after ymax lines have been received.
 */
void jbg85_dec_maxlen(struct jbg85_dec_state *s, unsigned long ymax)
{
  if (ymax > 0) s->ymax = ymax;
  return;
}


/*
 * Decode the new len PSDC bytes to which data points and output
 * decoded lines as they are completed. Return the number of bytes
 * which have actually been read. This will be less than len if a
 * marker segment was part of the data or if the final byte was
 * 0xff, meaning that this code can not determine whether we have a
 * marker segment.
 */
static size_t decode_pscd(struct jbg85_dec_state *s, unsigned char *data,
			  size_t len)
{
  unsigned char *hp1, *hp2, *hp3, *p1;
  register unsigned long line_h1, line_h2, line_h3;
  unsigned long x;
  long o;
  unsigned a;
  int n;
  int pix, slntp;
  int buflines = 3 - !!(s->options & JBG_LRLTWO);

  /* forward data to arithmetic decoder */
  s->s.pscd_ptr = data;
  s->s.pscd_end = data + len;
  
  /* restore a few local variables */
  line_h1 = s->line_h1;
  line_h2 = s->line_h2;
  line_h3 = s->line_h3;
  x = s->x;

#ifdef DEBUG
  if (s->x == 0 && s->i == 0 && s->pseudo)
    fprintf(stderr, "decode_pscd(%p, %p, %ld)\n",
	    (void *) s, (void *) data, (long) len);
#endif

  for (; s->i < s->l0 && s->y < s->y0; s->i++, s->y++) {

    /* pointer to image byte */
    hp1  = s->linebuf + s->p[0] * s->bpl + (s->x >> 3);
    hp2  = s->linebuf + s->p[1] * s->bpl + (s->x >> 3);
    hp3  = s->linebuf + s->p[2] * s->bpl + (s->x >> 3);

    /* adaptive template changes */
    if (x == 0 && s->pseudo)
      for (n = 0; n < s->at_moves; n++)
	if (s->at_line[n] == s->i) {
	  s->tx = s->at_tx[n];
#ifdef DEBUG
	  fprintf(stderr, "ATMOVE: line=%lu, tx=%d.\n", s->i, s->tx);
#endif
	}
    assert(s->tx >= 0); /* i.e., tx can safely be cast to unsigned */
    
    /* typical prediction */
    if (s->options & JBG_TPBON && s->pseudo) {
      slntp = arith_decode(&s->s, (s->options & JBG_LRLTWO) ? TPB2CX : TPB3CX);
      if (s->s.result == JBG_MORE || s->s.result == JBG_MARKER)
	goto leave;
      s->lntp =
	!(slntp ^ s->lntp);
      if (!s->lntp) {
	/* this line is 'typical' (i.e. identical to the previous one) */
	s->p[2] = s->p[1];
	s->p[1] = s->p[0];
	if (s->y == 0 || s->reset) {
	  for (p1 = hp1; p1 < hp1 + s->bpl; *p1++ = 0);
	  s->line_out(hp1, s->bpl, s->y, s->file);
	  if (++(s->p[0]) >= buflines) s->p[0] = 0;
	} else {
	  s->line_out(hp1, s->bpl, s->y, s->file);
	}
	continue;
      }
      /* this line is 'not typical' and has to be coded completely */
    }
    s->pseudo = 0;
    
    /*
     * Layout of the variables line_h1, line_h2, line_h3, which contain
     * as bits the neighbour pixels of the currently decoded pixel X:
     *
     *                     76543210 76543210 76543210 76543210     line_h3
     *                     76543210 76543210 76543210 76543210     line_h2
     *   76543210 76543210 76543210 76543210 X                     line_h1
     */
    
    if (x == 0) {
      line_h1 = line_h2 = line_h3 = 0;
      if (s->p[1] >= 0)
	line_h2 = (long)*hp2 << 8;
      if (s->p[2] >= 0)
	line_h3 = (long)*hp3 << 8;
    }
    
    /* decode line */
    while (x < s->x0) {
      if ((x & 7) == 0) {
	if (x < (s->bpl - 1) * 8 && s->p[1] >= 0) {
	  line_h2 |= *(hp2 + 1);
	  if (s->p[2] >= 0)
	    line_h3 |= *(hp3 + 1);
	}
      }
      if (s->options & JBG_LRLTWO) {
	/* two line template */
	do {
	  if (s->tx) {
	    if ((unsigned) s->tx > x)
	      a = 0;
	    else if (s->tx < 8)
	      a = ((line_h1 >> (s->tx - 5)) & 0x010);
	    else {
	      o = (x - s->tx) - (x & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      a <<= 4;
	    }
	    assert(s->tx > 31 ||
		   a == ((line_h1 >> (s->tx - 5)) & 0x010));
	    pix = arith_decode(&s->s, (((line_h2 >> 9) & 0x3e0) | a |
				       (line_h1 & 0x00f)));
	  } else
	    pix = arith_decode(&s->s, (((line_h2 >> 9) & 0x3f0) |
				       (line_h1 & 0x00f)));
	  if (s->s.result == JBG_MORE || s->s.result == JBG_MARKER)
	    goto leave;
	  line_h1 = (line_h1 << 1) | pix;
	  line_h2 <<= 1;
	} while ((++x & 7) && x < s->x0);
      } else {
	/* three line template */
	do {
	  if (s->tx) {
	    if ((unsigned) s->tx > x)
	      a = 0;
	    else if (s->tx < 8)
	      a = ((line_h1 >> (s->tx - 3)) & 0x004);
	    else {
	      o = (x - s->tx) - (x & ~7L);
	      a = (hp1[o >> 3] >> (7 - (o & 7))) & 1;
	      a <<= 2;
	    }
	    assert(s->tx > 31 ||
		   a == ((line_h1 >> (s->tx - 3)) & 0x004));
	    pix = arith_decode(&s->s, (((line_h3 >>  7) & 0x380) |
				       ((line_h2 >> 11) & 0x078) | a |
				       (line_h1 & 0x003)));
	  } else
	    pix = arith_decode(&s->s, (((line_h3 >>  7) & 0x380) |
				       ((line_h2 >> 11) & 0x07c) |
				       (line_h1 & 0x003)));
	  if (s->s.result == JBG_MORE || s->s.result == JBG_MARKER)
	    goto leave;
	  line_h1 = (line_h1 << 1) | pix;
	  line_h2 <<= 1;
	  line_h3 <<= 1;
	} while ((++x & 7) && x < s->x0);
      } /* if (s->options & JBG_LRLTWO) */
      *hp1++ = line_h1;
      hp2++;
      hp3++;
    } /* while (x < s->x0) */
    *(hp1 - 1) <<= s->bpl * 8 - s->x0;
    s->line_out(s->linebuf + s->p[0] * s->bpl, s->bpl, s->y, s->file);
    x = 0;
    s->pseudo = 1;
  } /* for (i = ...) */
  
 leave:

  /* save a few local variables */
  s->line_h1 = line_h1;
  s->line_h2 = line_h2;
  s->line_h3 = line_h3;
  s->x = x;

  return s->s.pscd_ptr - data;
}


/*
 * Provide a new BIE fragment to the decoder.
 *
 * If cnt is not NULL, then *cnt will contain after the call the
 * number of actually read bytes. If the data was not complete, then
 * the return value will be JBG_EAGAIN and *cnt == len. In case this
 * function has returned with JBG_EOK, then it has reached the end of
 * a BIE and the full image has been decoded. In case the return value
 * was JBG_EOK_INTR then this function can be called again with the
 * rest of the BIE, because parsing the BIE has been interrupted by a
 * jbg_dec_maxlen() specification. In this case the remaining len -
 * *cnt bytes of the previous block will have to passed to this
 * function again (if len > *cnt). In case of any other return value
 * than JBG_EOK, JBG_EOK_INTR or JBG_EAGAIN, a serious problem has
 * occured and the only function you can still call is jbg_strerror()
 * in order to find out what to tell the user.
 */
int jbg85_dec_in(struct jbg85_dec_state *s, unsigned char *data, size_t len,
	       size_t *cnt)
{
  int required_length;
  unsigned long y;
  size_t dummy_cnt;

  if (!cnt) cnt = &dummy_cnt;
  *cnt = 0;
  if (len < 1) return JBG_EAGAIN;

  /* read in 20-byte BIH */
  if (s->bie_len < 20) {
    while (s->bie_len < 20 && *cnt < len)
      s->buffer[s->bie_len++] = data[(*cnt)++];
    if (s->bie_len < 20) 
      return JBG_EAGAIN;
    /* test whether this looks like a valid JBIG header at all */
    if (s->buffer[1] < s->buffer[0])
      return JBG_EINVAL;
    if (s->buffer[3] != 0 || (s->buffer[18] & 0xf0) != 0 ||
	(s->buffer[19] & 0x80) != 0)
      return JBG_EINVAL; /* padding bits are not zero as required */
    s->x0 = (((long) s->buffer[ 4] << 24) | ((long) s->buffer[ 5] << 16) |
	     ((long) s->buffer[ 6] <<  8) | (long) s->buffer[ 7]);
    s->y0 = (((long) s->buffer[ 8] << 24) | ((long) s->buffer[ 9] << 16) |
	     ((long) s->buffer[10] <<  8) | (long) s->buffer[11]);
    s->l0 = (((long) s->buffer[12] << 24) | ((long) s->buffer[13] << 16) |
	     ((long) s->buffer[14] <<  8) | (long) s->buffer[15]);
    if (!s->x0 || !s->y0 || !s->l0)
      return JBG_EINVAL;
    s->mx = s->buffer[16];
    if (s->mx > 127)
      return JBG_EINVAL;
    if (s->buffer[ 0] != 0 || s->buffer[ 1] != 0 || s->buffer[ 2] != 1 ||
	s->buffer[17] != 0 || s->buffer[18] != 0)
      return JBG_EIMPL; /* parameters outside T.85 */
    s->options = s->buffer[19];
    if (s->options & 0x17)
      return JBG_EIMPL; /* parameters outside T.85 */
    if (s->x0 > (s->linebuf_len / ((s->options & JBG_LRLTWO) ? 2 : 3)) * 8)
      return JBG_ENOMEM; /* provided line buffer is too short */
    s->comment_skip = 0;
    s->buf_len = 0;
    s->x = 0;
    s->y = 0;
    s->i = 0;
    s->pseudo = 1;
    s->at_moves = 0;
    s->tx = 0;
    s->lntp = 1;
    s->bpl = (s->x0 >> 3) + !!(s->x0 & 7); /* bytes per line */
    s->p[0] = 0;
    s->p[1] = -1;
    s->p[2] = -1;
  }

  /*
   * BID processing loop
   */
  
  while (*cnt < len) {

    /* process floating marker segments */

    /* skip COMMENT contents */
    if (s->comment_skip) {
      if (s->comment_skip <= len - *cnt) {
	*cnt += s->comment_skip;
	s->comment_skip = 0;
      } else {
	s->comment_skip -= len - *cnt;
	*cnt = len;
      }
      continue;
    }
    
    /* load complete marker segments into s->buffer for processing */
    if (s->buf_len > 0) {
      assert(s->buffer[0] == MARKER_ESC);
      while (s->buf_len < 2 && *cnt < len)
	s->buffer[s->buf_len++] = data[(*cnt)++];
      if (s->buf_len < 2) continue;
      switch (s->buffer[1]) {
      case MARKER_COMMENT: required_length = 6; break;
      case MARKER_ATMOVE:  required_length = 8; break;
      case MARKER_NEWLEN:  required_length = 6; break;
      case MARKER_ABORT:
      case MARKER_SDNORM:
      case MARKER_SDRST:   required_length = 2; break;
      case MARKER_STUFF:
	/* forward stuffed 0xff to arithmetic decoder */
	s->buf_len = 0;
	decode_pscd(s, s->buffer, 2);
	continue;
      default:
	return JBG_EMARKER;
      }
      while (s->buf_len < required_length && *cnt < len)
	s->buffer[s->buf_len++] = data[(*cnt)++];
      if (s->buf_len < required_length) continue;
      /* now the buffer is filled with exactly one marker segment */
      switch (s->buffer[1]) {
      case MARKER_COMMENT:
	s->comment_skip =
	  (((long) s->buffer[2] << 24) | ((long) s->buffer[3] << 16) |
	   ((long) s->buffer[4] <<  8) | (long) s->buffer[5]);
	break;
      case MARKER_ATMOVE:
	if (s->at_moves < JBG85_ATMOVES_MAX) {
	  s->at_line[s->at_moves] =
	    (((long) s->buffer[2] << 24) | ((long) s->buffer[3] << 16) |
	     ((long) s->buffer[4] <<  8) | (long) s->buffer[5]);
	  s->at_tx[s->at_moves] = (signed char) s->buffer[6];
	  if (s->at_tx[s->at_moves] > (int) s->mx ||
	      (s->at_tx[s->at_moves] < ((s->options & JBG_LRLTWO) ? 5 : 3) &&
	       s->at_tx[s->at_moves] != 0) ||
	      s->buffer[7] != 0)
	    return JBG_EINVAL;
	  s->at_moves++;
	} else
	  return JBG_EIMPL;
	break;
      case MARKER_NEWLEN:
	y = (((long) s->buffer[2] << 24) | ((long) s->buffer[3] << 16) |
	     ((long) s->buffer[4] <<  8) | (long) s->buffer[5]);
	if (y > s->y0 || !(s->options & JBG_VLENGTH))
	  return JBG_EINVAL;
	s->y0 = y;
	break;
      case MARKER_ABORT:
	return JBG_EABORT;
	
      case MARKER_SDNORM:
      case MARKER_SDRST:
	/* decode final pixels based on trailing zero bytes */
	decode_pscd(s, s->buffer, 2);

	s->reset = (s->buffer[1] == MARKER_SDRST);
	arith_decode_init(&s->s, !s->reset);
	
	/* prepare for next SDE */
	s->x = 0;
	s->i = 0;
	s->pseudo = 1;
	s->at_moves = 0;
	if (s->reset) {
	  s->tx = 0;
	  s->lntp = 1;
	}
	
	s->buf_len = 0;
	
	/* check whether this was the last SDE */
	if (s->y >= s->y0) {
#ifdef DEBUG
	  fprintf(stderr, "This was the final SDE in this BIE, "
		  "%d bytes left.\n", len - *cnt);
#endif
	  return JBG_EOK;
	}

	/* check whether we have to abort because of ymax */
	if (s->y >= s->ymax) {
	  s->ymax = 4294967295UL;
	  return JBG_EOK_INTR;
	}
	/* todo: should check each line, not just after each SDE */

	break;
      }
      s->buf_len = 0;

    } else if (data[*cnt] == MARKER_ESC)
      s->buffer[s->buf_len++] = data[(*cnt)++];

    else {

      /* we have found PSCD bytes */
      *cnt += decode_pscd(s, data + *cnt, len - *cnt);
      if (*cnt < len && data[*cnt] != MARKER_ESC) {
#ifdef DEBUG
	fprintf(stderr, "PSCD was longer than expected, unread bytes "
		"%02x %02x %02x %02x ...\n", data[*cnt], data[*cnt+1],
		data[*cnt+2], data[*cnt+3]);
#endif
	return JBG_EINVAL;
      }
      
    }
  }  /* of BID processing loop 'while (*cnt < len) ...' */

  return JBG_EAGAIN;
}


#if TODO
/*
 * After jbg_dec_in() returned JBG_EOK or JBG_EOK_INTR, you can call this
 * function in order to find out the width of the image.
 */
long jbg85_dec_getwidth(const struct jbg85_dec_state *s)
{
  return s->x0;
}


/*
 * After jbg_dec_in() returned JBG_EOK or JBG_EOK_INTR, you can call this
 * function in order to find out the height of the image.
 */
long jbg85_dec_getheight(const struct jbg85_dec_state *s)
{
  return s->y0;
}
#endif
