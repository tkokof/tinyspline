#include "tinyspline.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

/********************************************************
*                                                       *
* Internal functions                                    *
*                                                       *
********************************************************/

/**
 * @return ::TS_SUCCESS, ::TS_MULTIPLICITY, ::TS_OVER_UNDERFLOW
 */
tsError ts_internal_bspline_insert_knot(
    const tsBSpline* bspline, const tsDeBoorNet* deBoorNet, const size_t n, 
    tsBSpline* result
)
{
    tsError err;

    if (deBoorNet->s+n > bspline->order)
        goto err_multiplicity;
    if (((int)n) < 0) // ensure n fits into an int without getting negative
        goto err_over_underflow;

    err = ts_bspline_resize(bspline, (int)n, 1, result);
    if (err < 0)
        return err;
    if (n == 0) // nothing to insert
        return TS_SUCCESS;

    const size_t deg = bspline->deg;
    const size_t dim = bspline->dim;  
    const size_t k = deBoorNet->k;
    const size_t N = deBoorNet->h+1; // <- the number of affected ctrlps
                                     //    n > 0 implies s <= deg implies a
                                     //    regular evaluation implies h+1 is
                                     //    valid
    const size_t size_ctrlp = dim * sizeof(float);

    // 1. Copy all necessary control points and knots from 
    //    the original B-Spline.
    // 2. Copy all necessary control points and knots from
    //    the de Boor net.
    
    // 1.
    //
    // a) copy left hand side control points from original b-spline
    // b) copy right hand side control points from original b-spline
    // c) copy left hand side knots from original b-spline
    // d) copy right hand side knots form original b-spline
    const size_t cidx = k-deg+N;
    const size_t kidx = k+1;
    // copy control points
    memmove(result->ctrlp, bspline->ctrlp, (k-deg) * size_ctrlp);
    memmove(
        result->ctrlp + (cidx+n)*dim, // n >= 0 implies cidx+n >= cidx
        bspline->ctrlp + cidx*dim, 
        (result->n_ctrlp-n-cidx) * size_ctrlp
    );
    // copy knots
    memmove(result->knots, bspline->knots, (k+1) * sizeof(float));
    memmove(
        result->knots+kidx+n, // n >= 0 implies kidx+n >= kidx
        bspline->knots+kidx, 
        (result->n_knots-n-kidx) * sizeof(float)
    );
    
    // 2.
    //
    // a) copy left hand side control points from de boor net
    // b) copy middle part control points from de boor net
    // c) copy right hand side control points from de boor net
    // d) insert knots with u_k
    size_t i; // <- used in for loops
    float* from = deBoorNet->points;
    float* to = result->ctrlp + (k-deg)*dim;
    int stride = (int)(N*dim); // <- will be negative in c), thus use int
    if (stride < 0)
        goto err_over_underflow;

    // copy control points
    for (i = 0; i < n; i++) { // a)
        memcpy(to, from, size_ctrlp);
        from += stride;
        to += dim;
        stride -= dim;
    }
    memcpy(to, from, (N-n) * size_ctrlp); // b)

    from -= dim;
    to += (N-n)*dim;
    stride = (int)(-(N-n+1)*dim);
    if (stride >= 0)
        goto err_over_underflow;

    for (i = 0; i < n; i++) { // c)
        memcpy(to, from, size_ctrlp);
        from += stride;
        stride -= dim;
        to += dim;
    }
    // copy knots
    to = result->knots+k+1;
    for (i = 0; i < n; i++) { // d)
        *to = deBoorNet->u;
        to++;
    }
    return TS_SUCCESS;
    
    // error handling
    err_multiplicity:
        err = TS_MULTIPLICITY;
        goto cleanup;
    err_over_underflow:
        err = TS_OVER_UNDERFLOW;
        goto cleanup;
    cleanup:
        if (bspline != result)
            ts_bspline_default(result);
        return err;
}


/********************************************************
*                                                       *
* Interface implementation                              *
*                                                       *
********************************************************/
void ts_deboornet_default(tsDeBoorNet* deBoorNet)
{
    deBoorNet->u        = 0.f;
    deBoorNet->k        = 0;
    deBoorNet->s        = 0;
    deBoorNet->h        = 0;
    deBoorNet->dim      = 0;
    deBoorNet->n_points = 0;
    deBoorNet->points   = NULL;
    deBoorNet->result   = NULL;
}

void ts_deboornet_free(tsDeBoorNet* deBoorNet)
{
    if (deBoorNet->points != NULL) {
        // automatically frees the field result
        free(deBoorNet->points);
    }
    ts_deboornet_default(deBoorNet);
}

void ts_bspline_default(tsBSpline* bspline)
{
    bspline->deg     = 0;
    bspline->order   = 0;
    bspline->dim     = 0;
    bspline->n_ctrlp = 0;
    bspline->n_knots = 0;
    bspline->ctrlp   = NULL;
    bspline->knots   = NULL;
}

void ts_bspline_free(tsBSpline* bspline)
{
    if (bspline->ctrlp != NULL) {
        free(bspline->ctrlp);
    }
    if (bspline->knots != NULL) {
        free(bspline->knots);
    }
    ts_bspline_default(bspline);
}

tsError ts_bspline_new(
    const size_t deg, const size_t dim, const size_t n_ctrlp, const tsBSplineType type,
    tsBSpline* bspline
)
{
    tsError err;
    ts_bspline_default(bspline);
    
    if (dim < 1)
        goto err_dim_zero;
    if (deg >= n_ctrlp)
        goto err_deg_ge_nctrlp;
    
    const size_t order = deg + 1;
    if (order < deg)
        goto err_over_underflow;
    const size_t n_knots = n_ctrlp + order;
    if (n_knots < n_ctrlp)
        goto err_over_underflow;
    const size_t size_flt = sizeof(float);
    
    // setup b-spline
    bspline->deg     = deg;
    bspline->order   = order;
    bspline->dim     = dim;
    bspline->n_ctrlp = n_ctrlp;
    bspline->n_knots = n_knots;
    bspline->ctrlp   = (float*) malloc(n_ctrlp*dim*size_flt);
    if (bspline->ctrlp == NULL)
        goto err_malloc;
    bspline->knots   = (float*) malloc(n_knots*size_flt);
    if (bspline->knots == NULL)
        goto err_malloc;
    ts_bspline_setup_knots(bspline, type, bspline);
    return TS_SUCCESS;
    
    // error handling
    err_dim_zero:
        err = TS_DIM_ZERO;
        goto cleanup;
    err_deg_ge_nctrlp:
        err = TS_DEG_GE_NCTRLP;
        goto cleanup;
    err_over_underflow:
        err = TS_OVER_UNDERFLOW;
        goto cleanup;
    err_malloc:
        err = TS_MALLOC;
        goto cleanup;
    cleanup:
        ts_bspline_free(bspline);
        return err;
}

tsError ts_bspline_setup_knots(
    const tsBSpline* original, const tsBSplineType type,
    tsBSpline* result
)
{
    if (original != result) {
        const tsError ret = ts_bspline_copy(original, result);
        if (ret < 0)
            return ret;
    }
    
    if (type == TS_NONE)
        return TS_SUCCESS;
    
    const size_t n_knots = result->n_knots;
    const size_t deg = result->deg;
    const size_t order = result->order;
    
    size_t current, end; // <- used by loops
    size_t numerator, dominator; // <- to fill uniformly spaced elements
    
    if (type == TS_OPENED) {
        current = numerator = 0;
        end = n_knots;
        dominator = end-1;
        for (;current < end; current++, numerator++)
            result->knots[current] = (float) numerator / dominator;
    } else {
        current = 0;
        end = order;
        for (;current < end; current++)
            result->knots[current] = 0.f;
        end = n_knots - order;
        numerator = 1;
        dominator = n_knots - (2 * deg) - 1;
        for (;current < end; current++, numerator++)
            result->knots[current] = (float) numerator / dominator;
        end = n_knots;
        for (;current < end; current++)
            result->knots[current] = 1.f;
    }
    return TS_SUCCESS;
}

tsError ts_bspline_copy(
    const tsBSpline* original,
    tsBSpline* copy
)
{
    if (original == copy)
        return TS_INPUT_EQ_OUTPUT;

    const size_t dim      = original->dim;
    const size_t n_ctrlp  = original->n_ctrlp;
    const size_t n_knots  = original->n_knots;
    const size_t size_flt = sizeof(float);

    copy->deg     = original->deg;
    copy->order   = original->order;
    copy->dim     = original->dim;
    copy->n_ctrlp = original->n_ctrlp;
    copy->n_knots = original->n_knots;
    copy->ctrlp   = (float*) malloc(n_ctrlp*dim*size_flt);
    if (copy->ctrlp == NULL)
        goto err_malloc;
    copy->knots   = (float*) malloc(n_knots*size_flt);
    if (copy->knots == NULL)
        goto err_malloc;
    memcpy(copy->ctrlp, original->ctrlp, n_ctrlp*dim*size_flt);
    memcpy(copy->knots, original->knots, n_knots*size_flt);
    return TS_SUCCESS;

    // error handling
    err_malloc:
        ts_bspline_free(copy);
        return TS_MALLOC;
}

tsError ts_bspline_evaluate(
    const tsBSpline* bspline, const float u, 
    tsDeBoorNet* deBoorNet
)
{
    tsError err;
    ts_deboornet_default(deBoorNet);
    
    // 1. Find index k such that u is in between [u_k, u_k+1).
    // 2. Check whether b-spline is defined at u
    // 3. Setup already known values
    // 4. Decide by multiplicity of u how to calculate point P(u).
    
    const size_t n_knots = bspline->n_knots; // <- the number of knots
    
    // 1. find index k
    for (deBoorNet->k = 0; deBoorNet->k < n_knots; deBoorNet->k++) {
        const float uk = bspline->knots[deBoorNet->k];
        if (ts_fequals(u, uk)) {
            deBoorNet->s++;
        } else if (u < uk) {
            break;
        }
    }
    
    const size_t s   = deBoorNet->s; // <- the multiplicity of u
    const size_t deg = bspline->deg; // <- the degree of the b-spline
    
    // 2. check u
    //
    // 2a) Check for u < u_0
    // 2b) Check for u > u_max
    // 2c) Check whether b-spline is defined at u for opened b-splines
    if (deBoorNet->k == 0) { // u < u_0
        goto err_u_undefined;
    } else if (deBoorNet->k == n_knots && s == 0) { // u > u_max
        goto err_u_undefined;
    } else if (s <= deg) { // we may have an opened b-spline
        // (keep in mind that currently k is k+1)
        if (deBoorNet->k <= deg || deBoorNet->k > n_knots-deg + s-1) {
            goto err_u_undefined;
        }
    }
    
    // 3. setup already known values
    deBoorNet->dim   = bspline->dim;
    deBoorNet->k--; // by 2. k must be >= 1, thus -1 will never underflow
    const float uk = bspline->knots[deBoorNet->k]; // ensures that with any 
                                                   // float precision the knot
    deBoorNet->u = ts_fequals(u, uk) ? uk : u;     // vector stays valid
    deBoorNet->h = deg < s ? 0 : deg-s; // prevent underflow
 
    const size_t order = bspline->order; // <- the order of the b-spline
    const size_t dim   = bspline->dim;   // <- the dimension of one point
    const size_t k     = deBoorNet->k;   // <- the index k of the de boor net
    const size_t size_ctrlp = sizeof(float) * dim; // <- size of one ctrlp
    
    // 4. decide how to calculate P(u)
    //
    // 4a) Check for multiplicity > order.
    //     This is not allowed for any control point.
    // 4b) Check for multiplicity = order.
    //     Take the two points k-s and k-s + 1. If one of
    //     them doesn't exist, take only the other.
    // 4c) Use de boor algorithm to find point P(u). 
    if (s > order) {
        goto err_multiplicity;
    } else if (s == order) {
        // only one of the two control points exists
        if (k == deg ||                  // only the first control point
            k == bspline->n_knots - 1) { // only the last control point
            
            deBoorNet->points = (float*) malloc(size_ctrlp);
            if (deBoorNet->points == NULL)
                goto err_malloc;
            deBoorNet->result = deBoorNet->points;
            deBoorNet->n_points = 1;
            const size_t from = k == deg ? 0 : (k-s) * dim;
            memcpy(deBoorNet->points, bspline->ctrlp + from, size_ctrlp);
            return 1;
        } else {
            deBoorNet->points = (float*) malloc(2 * size_ctrlp);
            if (deBoorNet->points == NULL)
                goto err_malloc;
            deBoorNet->result = deBoorNet->points+dim;
            deBoorNet->n_points = 2;
            // copy both control points
            const size_t from = (k-s) * dim;
            memcpy(deBoorNet->points, bspline->ctrlp + from, 2 * size_ctrlp);
            return 2;
        }
    } else { // by 4a) and 4b) s <= deg (order = deg+1)
        const size_t fst = k-deg; // <- first affected control point, inclusive
                                  //    by 2c) s <= deg implies k > deg
        const size_t lst = k-s;   // <- last affected control point, inclusive
                                  //    s <= deg < k
        const size_t N   = lst-fst + 1; // <- the number of affected ctrlps
                                        //    lst <= fst implies N >= 1

        deBoorNet->n_points = N * (N+1) * 0.5f;
        deBoorNet->points = (float*) malloc(deBoorNet->n_points * size_ctrlp);
        if (deBoorNet->points == NULL)
            goto err_malloc;
        deBoorNet->result = deBoorNet->points + (deBoorNet->n_points-1)*dim;
        
        // copy initial values to output
        memcpy(deBoorNet->points, bspline->ctrlp + fst*dim, N * size_ctrlp);
        
        int idx_l  = 0;       // <- the current left index
        int idx_r  = dim;     // <- the current right index
        int idx_to = N * dim; // <- the current to index
        
        size_t r = 1;
        for (;r <= deBoorNet->h; r++) {
            size_t i = fst + r;
            for (; i <= lst; i++) {
                const float ui    = bspline->knots[i];
                const float a     = (deBoorNet->u - ui) / 
                                    (bspline->knots[i+deg-r+1] - ui);
                const float a_hat = 1.f-a;
                
                size_t d;
                for (d = 0; d < dim; d++) {
                    deBoorNet->points[idx_to++] = 
                        a_hat * deBoorNet->points[idx_l++] + 
                            a * deBoorNet->points[idx_r++];
                }
            }
            idx_l += dim; 
            idx_r += dim;
        }
        
        return 0;
    }
    
    // error handling
    err_malloc:
        err = TS_MALLOC;
        goto cleanup;
    err_u_undefined:
        err = TS_U_UNDEFINED;
        goto cleanup;
    err_multiplicity:
        err = TS_MULTIPLICITY;
        goto cleanup;
    cleanup:
        ts_deboornet_free(deBoorNet);
        return err;
}

tsError ts_bspline_insert_knot(
    const tsBSpline* bspline, const float u, const size_t n,
    tsBSpline* result, size_t* k
)
{
    tsError ret;
    tsDeBoorNet net;
    ret = ts_bspline_evaluate(bspline, u, &net);
    if (ret >= 0) {
        ret = ts_internal_bspline_insert_knot(bspline, &net, n, result);
        *k = ret >= 0 ? net.k+n : 0;
    } else {
        ts_bspline_default(result);
        *k = 0;
    }
    ts_deboornet_free(&net);
    return ret;
}

tsError ts_bspline_resize(
    const tsBSpline* bspline, const int n, const int back,
    tsBSpline* resized
)
{
    tsError err;

    // if n is 0 the b-spline must not be resized
    if (n == 0 && bspline != resized)
        return ts_bspline_copy(bspline, resized);
    if (n == 0 && bspline == resized)
        return TS_SUCCESS;
    
    const size_t deg = bspline->deg;
    const size_t n_ctrlp = bspline->n_ctrlp;
    const size_t new_n_ctrlp = n_ctrlp+n;
    
    // check input
    if (new_n_ctrlp <= deg)
        goto err_deg_ge_nctrlp;
    else if (n < 0 && new_n_ctrlp > n_ctrlp)
        goto err_over_underflow;
    else if (n > 0 && new_n_ctrlp < n_ctrlp)
        goto err_over_underflow;
    
    const size_t dim = bspline->dim;
    const size_t n_knots = bspline->n_knots;
    const size_t new_n_knots = n_knots+n;
    const size_t min_n_ctrlp = n < 0 ? new_n_ctrlp : n_ctrlp;
    const size_t min_n_knots = n < 0 ? new_n_knots : n_knots;
    
    if (bspline != resized) {
        err = ts_bspline_new(deg, dim, new_n_ctrlp, TS_NONE, resized);
        if (err < 0)
            return err;
    }

    float* from_ctrlp = bspline->ctrlp;
    float* from_knots = bspline->knots;
    float* to_ctrlp = resized->ctrlp;
    float* to_knots = resized->knots;
    if (!back && n < 0) {
        from_ctrlp -= n*dim;
        from_knots -= n;
    } else if (!back && n > 0) {
        to_ctrlp += n*dim;
        to_knots += n;
    }
    
    const size_t size_flt = sizeof(float);
    const size_t size_ctrlp = dim*size_flt;
    
    if (bspline != resized) {
        memcpy(to_ctrlp, from_ctrlp, min_n_ctrlp * size_ctrlp);
        memcpy(to_knots, from_knots, min_n_knots * size_flt);
    } else {
        if (!back && n < 0) {
            memmove(to_ctrlp, from_ctrlp, min_n_ctrlp * size_ctrlp);
            memmove(to_knots, from_knots, min_n_knots * size_flt);
        }
        float* tmp; // <- used to not loose pointer if realloc fails
        // resize control points
        tmp = (float*) realloc(resized->ctrlp, new_n_ctrlp * size_ctrlp);
        if (tmp == NULL)
            goto err_malloc;
        resized->ctrlp = tmp;
        resized->n_ctrlp = new_n_ctrlp;
        // resize knots
        tmp = (float*) realloc(resized->knots, new_n_knots * size_flt);
        if (tmp == NULL)
            goto err_malloc;
        resized->knots = tmp;
        resized->n_knots = new_n_knots;
        if (!back && n > 0) {
            memmove(to_ctrlp, from_ctrlp, min_n_ctrlp * size_ctrlp);
            memmove(to_knots, from_knots, min_n_knots * size_flt);
        }
    }
    
    return TS_SUCCESS;
    
    // error handling
    err_deg_ge_nctrlp:
        err = TS_DEG_GE_NCTRLP;
        goto cleanup;
    err_over_underflow:
        err = TS_OVER_UNDERFLOW;
        goto cleanup;
    err_malloc:
        err = TS_MALLOC;
        goto cleanup;
    cleanup:
        if (bspline != resized)
            ts_bspline_default(resized);
        return err;
}

tsError ts_bspline_split(
    const tsBSpline* bspline, const float u,
    tsBSpline* split, size_t* k
)
{
    tsError ret;
    tsDeBoorNet net;
    ret = ts_bspline_evaluate(bspline, u, &net);
    if (ret >= 1) {
        if (bspline != split)
            ret = ts_bspline_copy(bspline, split);
        *k = ret >= 0 ? net.k : 0;
    } else if (ret == 0) {
        ret = ts_internal_bspline_insert_knot(bspline, &net, net.h+1, split);
        *k = ret >= 0 ? net.k + net.h + 1 : 0;
    } else {
        if (bspline != split)
            ts_bspline_default(split);
        *k = 0;
    }
    ts_deboornet_free(&net);
    return ret;
}

tsError ts_bspline_buckle(
    const tsBSpline* bspline, const float b,
    tsBSpline* buckled
)
{
    if (bspline != buckled) {
        const tsError err = ts_bspline_copy(bspline, buckled);
        if (err < 0)
            return err;
    }
    
    const float b_hat  = 1.f-b;            // <- 1-b
    const size_t dim   = buckled->dim;     // <- dimension of one control point 
    const size_t N     = buckled->n_ctrlp; // <- number of control points
    const float* p0    = buckled->ctrlp;   // <- pointer to P0
    const float* pn_1  = p0 + (N-1)*dim;   // <- pointer to P_n-1
    
    size_t i, d;
    for (i = 0; i < N; i++) {
        for (d = 0; d < dim; d++) {
            buckled->ctrlp[i*dim + d] = 
                    b * buckled->ctrlp[i*dim + d] + 
                b_hat * (p0[d] + ((float)i / (N-1)) * (pn_1[d] - p0[d]));
        }
    }
    
    return TS_SUCCESS;
}

tsError ts_bspline_to_beziers(
    const tsBSpline* bspline,
    tsBSpline* beziers
)
{
    tsError err;
    
    if (bspline != beziers) {
        err = ts_bspline_copy(bspline, beziers);
        if (err < 0)
            return err;
    }
    
    const size_t deg = beziers->deg;
    const size_t order = beziers->order;
    
    // fix first control point if necessary
    const float u_min = beziers->knots[deg];
    if (!ts_fequals(beziers->knots[0], u_min)) {
        size_t k;
        err = ts_bspline_split(beziers, u_min, beziers, &k);
        if (err < 0)
            return err;
        const int resize = -1*deg + (deg*2 - k);
        err = ts_bspline_resize(beziers, resize, 0, beziers);
        if (err < 0)
            return err;
    }

    // fix last control point if necessary
    const float u_max = beziers->knots[beziers->n_knots - order];
    if (!ts_fequals(beziers->knots[beziers->n_knots-1], u_max)) {
        size_t k;
        err = ts_bspline_split(beziers, u_max, beziers, &k);
        if (err < 0)
            return err;
        const int resize = -1*deg + (k - (beziers->n_knots - order));
        err = ts_bspline_resize(beziers, resize, 1, beziers);
        if (err < 0)
            return err;
    }
    
    size_t k = order;
    while (k < beziers->n_knots - order) {
        err = ts_bspline_split(beziers, beziers->knots[k], beziers, &k);
        if (err < 0)
            return err;
        k++;
    }
    return TS_SUCCESS;
}

int ts_bspline_equals(
    const tsBSpline* x, const tsBSpline* y
)
{
    if (x->deg     != y->deg ||
        x->order   != y->order ||
        x->dim     != y->dim ||
        x->n_ctrlp != y->n_ctrlp ||
        x->n_knots != y->n_knots) {
        return 0;
    } else {
        size_t i;
        for (i = 0; i < x->n_ctrlp; i++) {
            if (!ts_fequals(x->ctrlp[i], y->ctrlp[i]))
                return 0;
        }
        for (i = 0; i < x->n_knots; i++) {
            if (!ts_fequals(x->knots[i], y->knots[i]))
                return 0;
        }
        return 1;
    }
}

int ts_fequals(const float x, const float y)
{
    if (fabs(x-y) <= FLT_MAX_ABS_ERROR) {
        return 1;
    } else {
        const float r = fabsf(x) > fabsf(y) ?
            fabsf((x-y) / x) : fabsf((x-y) / y);
        return r <= FLT_MAX_REL_ERROR;
    }
}
