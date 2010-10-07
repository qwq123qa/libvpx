/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/config.h"
#include "encodemb.h"
#include "reconinter.h"
#include "quantize.h"
#include "tokenize.h"
#include "invtrans.h"
#include "recon.h"
#include "reconintra.h"
#include "dct.h"
#include "vpx_mem/vpx_mem.h"

#if CONFIG_RUNTIME_CPU_DETECT
#define IF_RTCD(x) (x)
#else
#define IF_RTCD(x) NULL
#endif
void vp8_subtract_b_c(BLOCK *be, BLOCKD *bd, int pitch)
{
    unsigned char *src_ptr = (*(be->base_src) + be->src);
    short *diff_ptr = be->src_diff;
    unsigned char *pred_ptr = bd->predictor;
    int src_stride = be->src_stride;

    int r, c;

    for (r = 0; r < 4; r++)
    {
        for (c = 0; c < 4; c++)
        {
            diff_ptr[c] = src_ptr[c] - pred_ptr[c];
        }

        diff_ptr += pitch;
        pred_ptr += pitch;
        src_ptr  += src_stride;
    }
}

void vp8_subtract_mbuv_c(short *diff, unsigned char *usrc, unsigned char *vsrc, unsigned char *pred, int stride)
{
    short *udiff = diff + 256;
    short *vdiff = diff + 320;
    unsigned char *upred = pred + 256;
    unsigned char *vpred = pred + 320;

    int r, c;

    for (r = 0; r < 8; r++)
    {
        for (c = 0; c < 8; c++)
        {
            udiff[c] = usrc[c] - upred[c];
        }

        udiff += 8;
        upred += 8;
        usrc  += stride;
    }

    for (r = 0; r < 8; r++)
    {
        for (c = 0; c < 8; c++)
        {
            vdiff[c] = vsrc[c] - vpred[c];
        }

        vdiff += 8;
        vpred += 8;
        vsrc  += stride;
    }
}

void vp8_subtract_mby_c(short *diff, unsigned char *src, unsigned char *pred, int stride)
{
    int r, c;

    for (r = 0; r < 16; r++)
    {
        for (c = 0; c < 16; c++)
        {
            diff[c] = src[c] - pred[c];
        }

        diff += 16;
        pred += 16;
        src  += stride;
    }
}

static void vp8_subtract_mb(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x)
{
    ENCODEMB_INVOKE(&rtcd->encodemb, submby)(x->src_diff, x->src.y_buffer, x->e_mbd.predictor, x->src.y_stride);
    ENCODEMB_INVOKE(&rtcd->encodemb, submbuv)(x->src_diff, x->src.u_buffer, x->src.v_buffer, x->e_mbd.predictor, x->src.uv_stride);
}

void vp8_build_dcblock(MACROBLOCK *x)
{
    short *src_diff_ptr = &x->src_diff[384];
    int i;

    for (i = 0; i < 16; i++)
    {
        src_diff_ptr[i] = x->coeff[i * 16];
    }
}

void vp8_transform_mbuv(MACROBLOCK *x)
{
    int i;

    for (i = 16; i < 24; i += 2)
    {
        x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
            &x->block[i].coeff[0], 16);
    }
}


void vp8_transform_intra_mby(MACROBLOCK *x)
{
    int i;

    for (i = 0; i < 16; i += 2)
    {
        x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
            &x->block[i].coeff[0], 32);
    }

    // build dc block from 16 y dc values
    vp8_build_dcblock(x);

    // do 2nd order transform on the dc block
    x->short_walsh4x4(&x->block[24].src_diff[0],
        &x->block[24].coeff[0], 8);

}


void vp8_transform_mb(MACROBLOCK *x)
{
    int i;

    for (i = 0; i < 16; i += 2)
    {
        x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
            &x->block[i].coeff[0], 32);
    }

    // build dc block from 16 y dc values
    if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
        vp8_build_dcblock(x);

    for (i = 16; i < 24; i += 2)
    {
        x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
            &x->block[i].coeff[0], 16);
    }

    // do 2nd order transform on the dc block
    if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
        x->short_walsh4x4(&x->block[24].src_diff[0],
        &x->block[24].coeff[0], 8);

}

void vp8_transform_mby(MACROBLOCK *x)
{
    int i;

    for (i = 0; i < 16; i += 2)
    {
        x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
            &x->block[i].coeff[0], 32);
    }

    // build dc block from 16 y dc values
    if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
    {
        vp8_build_dcblock(x);
        x->short_walsh4x4(&x->block[24].src_diff[0],
            &x->block[24].coeff[0], 8);
    }
}


void vp8_stuff_inter16x16(MACROBLOCK *x)
{
    vp8_build_inter_predictors_mb_s(&x->e_mbd);
    /*
        // recon = copy from predictors to destination
        {
            BLOCKD *b = &x->e_mbd.block[0];
            unsigned char *pred_ptr = b->predictor;
            unsigned char *dst_ptr = *(b->base_dst) + b->dst;
            int stride = b->dst_stride;

            int i;
            for(i=0;i<16;i++)
                vpx_memcpy(dst_ptr+i*stride,pred_ptr+16*i,16);

            b = &x->e_mbd.block[16];
            pred_ptr = b->predictor;
            dst_ptr = *(b->base_dst) + b->dst;
            stride = b->dst_stride;

            for(i=0;i<8;i++)
                vpx_memcpy(dst_ptr+i*stride,pred_ptr+8*i,8);

            b = &x->e_mbd.block[20];
            pred_ptr = b->predictor;
            dst_ptr = *(b->base_dst) + b->dst;
            stride = b->dst_stride;

            for(i=0;i<8;i++)
                vpx_memcpy(dst_ptr+i*stride,pred_ptr+8*i,8);
        }
    */
}

#if !(CONFIG_REALTIME_ONLY)
#define RDCOST(RM,DM,R,D) ( ((128+(R)*(RM)) >> 8) + (DM)*(D) )
#define RDTRUNC(RM,DM,R,D) ( (128+(R)*(RM)) & 0xFF )

typedef struct vp8_token_state vp8_token_state;

struct vp8_token_state{
  int           rate;
  int           error;
  signed char   next;
  signed char   token;
  short         qc;
};

// TODO: experiments to find optimal multiple numbers
#define Y1_RD_MULT 1
#define UV_RD_MULT 1
#define Y2_RD_MULT 4

static const int plane_rd_mult[4]=
{
    Y1_RD_MULT,
    Y2_RD_MULT,
    UV_RD_MULT,
    Y1_RD_MULT
};

void vp8_optimize_b(MACROBLOCK *mb, int ib, int type,
                    ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                    const VP8_ENCODER_RTCD *rtcd)
{
    BLOCK *b;
    BLOCKD *d;
    vp8_token_state tokens[17][2];
    unsigned best_mask[2];
    const short *dequant_ptr;
    const short *coeff_ptr;
    short *qcoeff_ptr;
    short *dqcoeff_ptr;
    int eob;
    int i0;
    int rc;
    int x;
    int sz;
    int next;
    int path;
    int rdmult;
    int rddiv;
    int final_eob;
    int rd_cost0;
    int rd_cost1;
    int rate0;
    int rate1;
    int error0;
    int error1;
    int t0;
    int t1;
    int best;
    int band;
    int pt;
    int i;
    int err_mult = plane_rd_mult[type];

    b = &mb->block[ib];
    d = &mb->e_mbd.block[ib];

    /* Enable this to test the effect of RDO as a replacement for the dynamic
     *  zero bin instead of an augmentation of it.
     */
#if 0
    vp8_strict_quantize_b(b, d);
#endif

    dequant_ptr = &d->dequant[0][0];
    coeff_ptr = &b->coeff[0];
    qcoeff_ptr = d->qcoeff;
    dqcoeff_ptr = d->dqcoeff;
    i0 = !type;
    eob = d->eob;

    /* Now set up a Viterbi trellis to evaluate alternative roundings. */
    /* TODO: These should vary with the block type, since the quantizer does. */
    rdmult = (mb->rdmult << 2)*err_mult;
    rddiv = mb->rddiv;
    best_mask[0] = best_mask[1] = 0;
    /* Initialize the sentinel node of the trellis. */
    tokens[eob][0].rate = 0;
    tokens[eob][0].error = 0;
    tokens[eob][0].next = 16;
    tokens[eob][0].token = DCT_EOB_TOKEN;
    tokens[eob][0].qc = 0;
    *(tokens[eob] + 1) = *(tokens[eob] + 0);
    next = eob;
    for (i = eob; i-- > i0;)
    {
        int base_bits;
        int d2;
        int dx;

        rc = vp8_default_zig_zag1d[i];
        x = qcoeff_ptr[rc];
        /* Only add a trellis state for non-zero coefficients. */
        if (x)
        {
            int shortcut=0;
            error0 = tokens[next][0].error;
            error1 = tokens[next][1].error;
            /* Evaluate the first possibility for this state. */
            rate0 = tokens[next][0].rate;
            rate1 = tokens[next][1].rate;
            t0 = (vp8_dct_value_tokens_ptr + x)->Token;
            /* Consider both possible successor states. */
            if (next < 16)
            {
                band = vp8_coef_bands[i + 1];
                pt = vp8_prev_token_class[t0];
                rate0 +=
                    mb->token_costs[type][band][pt][tokens[next][0].token];
                rate1 +=
                    mb->token_costs[type][band][pt][tokens[next][1].token];
            }
            rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
            rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
            if (rd_cost0 == rd_cost1)
            {
                rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
                rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
            }
            /* And pick the best. */
            best = rd_cost1 < rd_cost0;
            base_bits = *(vp8_dct_value_cost_ptr + x);
            dx = dqcoeff_ptr[rc] - coeff_ptr[rc];
            d2 = dx*dx;
            tokens[i][0].rate = base_bits + (best ? rate1 : rate0);
            tokens[i][0].error = d2 + (best ? error1 : error0);
            tokens[i][0].next = next;
            tokens[i][0].token = t0;
            tokens[i][0].qc = x;
            best_mask[0] |= best << i;
            /* Evaluate the second possibility for this state. */
            rate0 = tokens[next][0].rate;
            rate1 = tokens[next][1].rate;

            if((abs(x)*dequant_ptr[rc]>abs(coeff_ptr[rc])) &&
               (abs(x)*dequant_ptr[rc]<abs(coeff_ptr[rc])+dequant_ptr[rc]))
                shortcut = 1;
            else
                shortcut = 0;

            if(shortcut)
            {
                sz = -(x < 0);
                x -= 2*sz + 1;
            }

            /* Consider both possible successor states. */
            if (!x)
            {
                /* If we reduced this coefficient to zero, check to see if
                 *  we need to move the EOB back here.
                 */
                t0 = tokens[next][0].token == DCT_EOB_TOKEN ?
                    DCT_EOB_TOKEN : ZERO_TOKEN;
                t1 = tokens[next][1].token == DCT_EOB_TOKEN ?
                    DCT_EOB_TOKEN : ZERO_TOKEN;
            }
            else
            {
                t0=t1 = (vp8_dct_value_tokens_ptr + x)->Token;
            }
            if (next < 16)
            {
                band = vp8_coef_bands[i + 1];
                if(t0!=DCT_EOB_TOKEN)
                {
                    pt = vp8_prev_token_class[t0];
                    rate0 += mb->token_costs[type][band][pt][
                        tokens[next][0].token];
                }
                if(t1!=DCT_EOB_TOKEN)
                {
                    pt = vp8_prev_token_class[t1];
                    rate1 += mb->token_costs[type][band][pt][
                        tokens[next][1].token];
                }
            }

            rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
            rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
            if (rd_cost0 == rd_cost1)
            {
                rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
                rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
            }
            /* And pick the best. */
            best = rd_cost1 < rd_cost0;
            base_bits = *(vp8_dct_value_cost_ptr + x);

            if(shortcut)
            {
                dx -= (dequant_ptr[rc] + sz) ^ sz;
                d2 = dx*dx;
            }
            tokens[i][1].rate = base_bits + (best ? rate1 : rate0);
            tokens[i][1].error = d2 + (best ? error1 : error0);
            tokens[i][1].next = next;
            tokens[i][1].token =best?t1:t0;
            tokens[i][1].qc = x;
            best_mask[1] |= best << i;
            /* Finally, make this the new head of the trellis. */
            next = i;
        }
        /* There's no choice to make for a zero coefficient, so we don't
         *  add a new trellis node, but we do need to update the costs.
         */
        else
        {
            band = vp8_coef_bands[i + 1];
            t0 = tokens[next][0].token;
            t1 = tokens[next][1].token;
            /* Update the cost of each path if we're past the EOB token. */
            if (t0 != DCT_EOB_TOKEN)
            {
                tokens[next][0].rate += mb->token_costs[type][band][0][t0];
                tokens[next][0].token = ZERO_TOKEN;
            }
            if (t1 != DCT_EOB_TOKEN)
            {
                tokens[next][1].rate += mb->token_costs[type][band][0][t1];
                tokens[next][1].token = ZERO_TOKEN;
            }
            /* Don't update next, because we didn't add a new node. */
        }
    }

    /* Now pick the best path through the whole trellis. */
    band = vp8_coef_bands[i + 1];
    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
    rate0 = tokens[next][0].rate;
    rate1 = tokens[next][1].rate;
    error0 = tokens[next][0].error;
    error1 = tokens[next][1].error;
    t0 = tokens[next][0].token;
    t1 = tokens[next][1].token;
    rate0 += mb->token_costs[type][band][pt][t0];
    rate1 += mb->token_costs[type][band][pt][t1];
    rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
    rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
    if (rd_cost0 == rd_cost1)
    {
        rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
        rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
    }
    best = rd_cost1 < rd_cost0;
    final_eob = i0 - 1;
    for (i = next; i < eob; i = next)
    {
        x = tokens[i][best].qc;
        if (x)
            final_eob = i;
        rc = vp8_default_zig_zag1d[i];
        qcoeff_ptr[rc] = x;
        dqcoeff_ptr[rc] = x * dequant_ptr[rc];
        next = tokens[i][best].next;
        best = (best_mask[best] >> i) & 1;
    }
    final_eob++;

    d->eob = final_eob;
    *a = *l = (d->eob != !type);
}

void vp8_optimize_mb(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd)
{
    int b;
    int type;
    int has_2nd_order;
    ENTROPY_CONTEXT_PLANES t_above, t_left;
    ENTROPY_CONTEXT *ta;
    ENTROPY_CONTEXT *tl;

    vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
    vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

    ta = (ENTROPY_CONTEXT *)&t_above;
    tl = (ENTROPY_CONTEXT *)&t_left;

    has_2nd_order = (x->e_mbd.mode_info_context->mbmi.mode != B_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
    type = has_2nd_order ? 0 : 3;

    for (b = 0; b < 16; b++)
    {
        vp8_optimize_b(x, b, type,
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }

    for (b = 16; b < 20; b++)
    {
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }

    for (b = 20; b < 24; b++)
    {
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }


    if (has_2nd_order)
    {
        b=24;
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }
}


void vp8_optimize_mby(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd)
{
    int b;
    int type;
    int has_2nd_order;

    ENTROPY_CONTEXT_PLANES t_above, t_left;
    ENTROPY_CONTEXT *ta;
    ENTROPY_CONTEXT *tl;

    if (!x->e_mbd.above_context)
        return;

    if (!x->e_mbd.left_context)
        return;

    vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
    vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

    ta = (ENTROPY_CONTEXT *)&t_above;
    tl = (ENTROPY_CONTEXT *)&t_left;

    has_2nd_order = (x->e_mbd.mode_info_context->mbmi.mode != B_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
    type = has_2nd_order ? 0 : 3;

    for (b = 0; b < 16; b++)
    {
        vp8_optimize_b(x, b, type,
        ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }


    if (has_2nd_order)
    {
        b=24;
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }
}

void vp8_optimize_mbuv(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd)
{
    int b;
    ENTROPY_CONTEXT_PLANES t_above, t_left;
    ENTROPY_CONTEXT *ta;
    ENTROPY_CONTEXT *tl;

    if (!x->e_mbd.above_context)
        return;

    if (!x->e_mbd.left_context)
        return;

    vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
    vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

    ta = (ENTROPY_CONTEXT *)&t_above;
    tl = (ENTROPY_CONTEXT *)&t_left;

    for (b = 16; b < 20; b++)
    {
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }

    for (b = 20; b < 24; b++)
    {
        vp8_optimize_b(x, b, vp8_block2type[b],
            ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    }

}
#endif

void vp8_encode_inter16x16(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x)
{
    vp8_build_inter_predictors_mb(&x->e_mbd);

    vp8_subtract_mb(rtcd, x);

    vp8_transform_mb(x);

    vp8_quantize_mb(x);

#if !(CONFIG_REALTIME_ONLY)
    if (x->optimize && x->rddiv > 1)
        vp8_optimize_mb(x, rtcd);
#endif

    vp8_inverse_transform_mb(IF_RTCD(&rtcd->common->idct), &x->e_mbd);

    vp8_recon16x16mb(IF_RTCD(&rtcd->common->recon), &x->e_mbd);
}


/* this funciton is used by first pass only */
void vp8_encode_inter16x16y(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x)
{
    vp8_build_inter_predictors_mby(&x->e_mbd);

    ENCODEMB_INVOKE(&rtcd->encodemb, submby)(x->src_diff, x->src.y_buffer, x->e_mbd.predictor, x->src.y_stride);

    vp8_transform_mby(x);

    vp8_quantize_mby(x);

    vp8_inverse_transform_mby(IF_RTCD(&rtcd->common->idct), &x->e_mbd);

    vp8_recon16x16mby(IF_RTCD(&rtcd->common->recon), &x->e_mbd);
}


void vp8_encode_inter16x16uv(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x)
{
    vp8_build_inter_predictors_mbuv(&x->e_mbd);

    ENCODEMB_INVOKE(&rtcd->encodemb, submbuv)(x->src_diff, x->src.u_buffer, x->src.v_buffer, x->e_mbd.predictor, x->src.uv_stride);

    vp8_transform_mbuv(x);

    vp8_quantize_mbuv(x);

    vp8_inverse_transform_mbuv(IF_RTCD(&rtcd->common->idct), &x->e_mbd);

    vp8_recon_intra_mbuv(IF_RTCD(&rtcd->common->recon), &x->e_mbd);
}


void vp8_encode_inter16x16uvrd(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x)
{
    vp8_build_inter_predictors_mbuv(&x->e_mbd);
    ENCODEMB_INVOKE(&rtcd->encodemb, submbuv)(x->src_diff, x->src.u_buffer, x->src.v_buffer, x->e_mbd.predictor, x->src.uv_stride);

    vp8_transform_mbuv(x);

    vp8_quantize_mbuv(x);

}
