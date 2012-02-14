/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define H5Z_PACKAGE		/*suppress error about including H5Zpkg	  */


#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5Fprivate.h"         /* File access                          */
#include "H5Iprivate.h"		/* IDs			  		*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5Oprivate.h"		/* Object headers		  	*/
#include "H5Pprivate.h"         /* Property lists                       */
#include "H5Sprivate.h"		/* Dataspaces         			*/
#include "H5Tprivate.h"		/* Datatypes         			*/
#include "H5Zpkg.h"		/* Data filters				*/

#ifdef H5_HAVE_FILTER_SZIP

#ifdef H5_HAVE_SZLIB_H
#   include "szlib.h"
#endif

/* Local function prototypes */
static htri_t H5Z_can_apply_szip(hid_t dcpl_id, hid_t type_id, hid_t space_id);
static herr_t H5Z_set_local_szip(hid_t dcpl_id, hid_t type_id, hid_t space_id);
static size_t H5Z_filter_szip (unsigned flags, size_t cd_nelmts,
    const unsigned cd_values[], size_t nbytes, size_t *buf_size, void **buf,
    void *lib_data);


/*-------------------------------------------------------------------------
 * Function:    H5Z_init_szip
 *
 * Purpose:     Registers the szip filter.
 *
 * Return:      Success: 0
 *              Failure: Negative
 *
 * Programmer:  Neil Fortner
 *              Monday, December 12, 2011
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Z_init_szip(void)
{
    H5Z_class_int_t     fclass;         /* Filter class */
    herr_t              ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Build filter class struct */
    fclass.version = H5Z_CLASS_T_VERS_3;        /* H5Z_class_t version */
    fclass.id = H5Z_FILTER_SZIP;                /* Filter id number */
    fclass.encoder_present = (unsigned)SZ_encoder_enabled(); /* encoder_present flag */
    fclass.decoder_present = 1;                 /* decoder_present flag (set to true) */
    fclass.name = "szip";                       /* Filter name for debugging */
    fclass.can_apply = H5Z_can_apply_szip;      /* The "can apply" callback */
    fclass.set_local = H5Z_set_local_szip;      /* The "set local" callback */
    fclass.filter.v2 = H5Z_filter_szip;         /* The actual filter function */

    /* Register the filter */
    if(H5Z_register(&fclass) < 0)
        HGOTO_ERROR(H5E_PLINE, H5E_CANTINIT, FAIL, "unable to register szip filter")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Z_init_szip() */


/*-------------------------------------------------------------------------
 * Function:	H5Z_can_apply_szip
 *
 * Purpose:	Check the parameters for szip compression for validity and
 *              whether they fit a particular dataset.
 *
 * Note:        This function currently range-checks for datatypes with
 *              8-bit boundaries (8, 16, 24, etc.).  It appears that the szip
 *              library can actually handle 1-24, 32 & 64 bit samples.  If
 *              this becomes important, we should make the checks below more
 *              sophisticated and have them check for n-bit datatypes of the
 *              correct size, etc. - QAK
 *
 * Return:	Success: Non-negative
 *		Failure: Negative
 *
 * Programmer:	Quincey Koziol
 *              Monday, April  7, 2003
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static htri_t
H5Z_can_apply_szip(hid_t UNUSED dcpl_id, hid_t type_id, hid_t UNUSED space_id)
{
    const H5T_t	*type;                  /* Datatype */
    unsigned dtype_size;                /* Datatype's size (in bits) */
    H5T_order_t dtype_order;            /* Datatype's endianness order */
    htri_t ret_value = TRUE;            /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Get datatype */
    if(NULL == (type = H5I_object_verify(type_id, H5I_DATATYPE)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a datatype")

    /* Get datatype's size, for checking the "bits-per-pixel" */
    if((dtype_size = (8 * H5T_get_size(type))) == 0)
	HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "bad datatype size")

    /* Range check datatype's size */
    if(dtype_size > 32 && dtype_size != 64)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FALSE, "invalid datatype size")

    /* Get datatype's endianness order */
    if((dtype_order = H5T_get_order(type)) == H5T_ORDER_ERROR)
	HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "can't retrieve datatype endianness order")

    /* Range check datatype's endianness order */
    /* (Note: this may not handle non-atomic datatypes well) */
    if(dtype_order != H5T_ORDER_LE && dtype_order != H5T_ORDER_BE)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FALSE, "invalid datatype endianness order")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Z_can_apply_szip() */


/*-------------------------------------------------------------------------
 * Function:	H5Z_set_local_szip
 *
 * Purpose:	Set the "local" dataset parameters for szip compression.
 *
 * Return:	Success: Non-negative
 *		Failure: Negative
 *
 * Programmer:	Quincey Koziol
 *              Monday, April  7, 2003
 *
 * Modifications: Used new logic to set the size of the scanline parameter.
 *                Now SZIP compression can be applied to the chunk
 *                of any shape and size with only one restriction: the number
 *                of elements in the chunk has to be not less than number
 *                of elements (pixels) in the block (cd_values[H5Z_SZIP_PARM_PPB]
 *                parameter).
 *                           Elena Pourmal, July 20, 2004
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Z_set_local_szip(hid_t dcpl_id, hid_t type_id, hid_t space_id)
{
    H5P_genplist_t *dcpl_plist; /* Property list pointer */
    const H5T_t	*type;          /* Datatype */
    const H5S_t	*ds;            /* Dataspace */
    unsigned flags;             /* Filter flags */
    size_t cd_nelmts = H5Z_SZIP_USER_NPARMS;     /* Number of filter parameters */
    unsigned cd_values[H5Z_SZIP_TOTAL_NPARMS];  /* Filter parameters */
    hsize_t dims[H5O_LAYOUT_NDIMS];             /* Dataspace (i.e. chunk) dimensions */
    int ndims;                  /* Number of (chunk) dimensions */
    H5T_order_t dtype_order;    /* Datatype's endianness order */
    size_t dtype_size;          /* Datatype's size (in bits) */
    size_t dtype_precision;     /* Datatype's precision (in bits) */
    size_t dtype_offset;        /* Datatype's offset (in bits) */
    hsize_t scanline;           /* Size of dataspace's fastest changing dimension */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Get the plist structure */
    if(NULL == (dcpl_plist = H5P_object_verify(dcpl_id, H5P_DATASET_CREATE)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID")

    /* Get datatype */
    if(NULL == (type = H5I_object_verify(type_id, H5I_DATATYPE)))
	HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a datatype")

    /* Get the filter's current parameters */
    if(H5P_get_filter_by_id(dcpl_plist, H5Z_FILTER_SZIP, &flags, &cd_nelmts, cd_values, 0, NULL, NULL) < 0)
	HGOTO_ERROR(H5E_PLINE, H5E_CANTGET, FAIL, "can't get szip parameters")

    /* Get datatype's size, for checking the "bits-per-pixel" */
    if((dtype_size = (8 * H5T_get_size(type))) == 0)
	HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "bad datatype size");

    /* Get datatype's precision, in case is less than full bits  */
    if((dtype_precision = H5T_get_precision(type)) == 0)
	HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "bad datatype precision");

    if(dtype_precision < dtype_size) {
        dtype_offset = H5T_get_offset(type);
        if(dtype_offset != 0)
            dtype_precision = dtype_size;
    } /* end if */
    if(dtype_precision > 24) {
	if(dtype_precision <= 32)
            dtype_precision = 32;
	else if(dtype_precision <= 64)
            dtype_precision = 64;
    } /* end if */

    /* Set "local" parameter for this dataset's "bits-per-pixel" */
    cd_values[H5Z_SZIP_PARM_BPP] = dtype_precision;

    /* Get dataspace */
    if(NULL == (ds = (H5S_t *)H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a data space")

    /* Get dimensions for dataspace */
    if((ndims = H5S_get_simple_extent_dims(ds, dims, NULL)) < 0)
        HGOTO_ERROR(H5E_PLINE, H5E_CANTGET, FAIL, "unable to get dataspace dimensions")

    /* Set "local" parameter for this dataset's "pixels-per-scanline" */
    /* (Use the chunk's fastest changing dimension size) */
    assert(ndims > 0);
    scanline = dims[ndims - 1];

    /* Adjust scanline if it is smaller than number of pixels per block or
       if it is bigger than maximum pixels per scanline, or there are more than
       SZ_MAX_BLOCKS_PER_SCANLINE blocks per scanline  */

    /* Check the pixels per block against the 'scanline' size */
    if(scanline < cd_values[H5Z_SZIP_PARM_PPB]) {
        hssize_t npoints;                   /* Number of points in the dataspace */

        /* Get number of elements for the dataspace;  use
           total number of elements in the chunk to define the new 'scanline' size */
        if((npoints = H5S_GET_EXTENT_NPOINTS(ds)) < 0)
            HGOTO_ERROR(H5E_PLINE, H5E_CANTGET, FAIL, "unable to get number of points in the dataspace")
        if(npoints < cd_values[H5Z_SZIP_PARM_PPB])
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "pixels per block greater than total number of elements in the chunk")
        scanline = MIN((cd_values[H5Z_SZIP_PARM_PPB] * SZ_MAX_BLOCKS_PER_SCANLINE), npoints);
    }
    else {
        if(scanline <= SZ_MAX_PIXELS_PER_SCANLINE)
            scanline = MIN((cd_values[H5Z_SZIP_PARM_PPB] * SZ_MAX_BLOCKS_PER_SCANLINE), scanline);
        else
            scanline = cd_values[H5Z_SZIP_PARM_PPB] * SZ_MAX_BLOCKS_PER_SCANLINE;
    } /* end else */

    /* Assign the final value to the scanline */
    H5_ASSIGN_OVERFLOW(cd_values[H5Z_SZIP_PARM_PPS],scanline,hsize_t,unsigned);

    /* Get datatype's endianness order */
    if((dtype_order = H5T_get_order(type)) == H5T_ORDER_ERROR)
	HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "bad datatype endianness order")

    /* Set the correct endianness flag for szip */
    /* (Note: this may not handle non-atomic datatypes well) */
    cd_values[H5Z_SZIP_PARM_MASK] &= ~(SZ_LSB_OPTION_MASK|SZ_MSB_OPTION_MASK);
    switch(dtype_order) {
        case H5T_ORDER_LE:      /* Little-endian byte order */
            cd_values[H5Z_SZIP_PARM_MASK] |= SZ_LSB_OPTION_MASK;
            break;

        case H5T_ORDER_BE:      /* Big-endian byte order */
            cd_values[H5Z_SZIP_PARM_MASK] |= SZ_MSB_OPTION_MASK;
            break;

        default:
            HGOTO_ERROR(H5E_PLINE, H5E_BADTYPE, FAIL, "bad datatype endianness order")
    } /* end switch */

    /* Modify the filter's parameters for this dataset */
    if(H5P_modify_filter(dcpl_plist, H5Z_FILTER_SZIP, flags, H5Z_SZIP_TOTAL_NPARMS, cd_values) < 0)
	HGOTO_ERROR(H5E_PLINE, H5E_CANTSET, FAIL, "can't set local szip parameters")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Z_set_local_szip() */


/*-------------------------------------------------------------------------
 * Function:	H5Z_filter_szip
 *
 * Purpose:	Implement an I/O filter around the 'rice' algorithm in
 *              libsz
 *
 * Return:	Success: Size of buffer filtered
 *		Failure: 0
 *
 * Programmer:	Kent Yang
 *              Tuesday, April 1, 2003
 *
 * Modifications:
 *              Quincey Koziol, April 2, 2003
 *              Cleaned up code.
 *
 *-------------------------------------------------------------------------
 */
static size_t
H5Z_filter_szip (unsigned flags, size_t cd_nelmts, const unsigned cd_values[],
    size_t nbytes, size_t *buf_size, void **buf, void *lib_data)
{
    size_t ret_value = 0;       /* Return value */
    size_t size_out  = 0;       /* Size of output buffer */
    unsigned char *outbuf = NULL;    /* Pointer to new output buffer */
    unsigned char *newbuf = NULL;    /* Pointer to input buffer */
    SZ_com_t sz_param;          /* szip parameter block */

    FUNC_ENTER_NOAPI(0)

    /* Sanity check to make certain that we haven't drifted out of date with
     * the mask options from the szlib.h header */
    assert(H5_SZIP_ALLOW_K13_OPTION_MASK==SZ_ALLOW_K13_OPTION_MASK);
    assert(H5_SZIP_CHIP_OPTION_MASK==SZ_CHIP_OPTION_MASK);
    assert(H5_SZIP_EC_OPTION_MASK==SZ_EC_OPTION_MASK);
    assert(H5_SZIP_LSB_OPTION_MASK==SZ_LSB_OPTION_MASK);
    assert(H5_SZIP_MSB_OPTION_MASK==SZ_MSB_OPTION_MASK);
    assert(H5_SZIP_NN_OPTION_MASK==SZ_NN_OPTION_MASK);
    assert(H5_SZIP_RAW_OPTION_MASK==SZ_RAW_OPTION_MASK);

    /* Check arguments */
    if (cd_nelmts!=4)
	HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, 0, "invalid deflate aggression level")

    /* Copy the filter parameters into the szip parameter block */
    H5_ASSIGN_OVERFLOW(sz_param.options_mask,cd_values[H5Z_SZIP_PARM_MASK],unsigned,int);
    H5_ASSIGN_OVERFLOW(sz_param.bits_per_pixel,cd_values[H5Z_SZIP_PARM_BPP],unsigned,int);
    H5_ASSIGN_OVERFLOW(sz_param.pixels_per_block,cd_values[H5Z_SZIP_PARM_PPB],unsigned,int);
    H5_ASSIGN_OVERFLOW(sz_param.pixels_per_scanline,cd_values[H5Z_SZIP_PARM_PPS],unsigned,int);

    /* Input; uncompress */
    if (flags & H5Z_FLAG_REVERSE) {
        uint32_t stored_nalloc;  /* Number of bytes the compressed block will expand into */
        size_t nalloc;  /* Number of bytes the compressed block will expand into */

        /* Get the size of the uncompressed buffer */
        newbuf = (unsigned char *)*buf;
        UINT32DECODE(newbuf,stored_nalloc);
        H5_ASSIGN_OVERFLOW(nalloc,stored_nalloc,uint32_t,size_t);

        /* Allocate space for the uncompressed buffer */
        if(NULL == (outbuf = (unsigned char *)H5Z_aligned_malloc(nalloc, lib_data)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, 0, "memory allocation failed for szip decompression")

        /* Decompress the buffer */
        size_out=nalloc;
        if(SZ_BufftoBuffDecompress(outbuf, &size_out, newbuf, nbytes-4, &sz_param) != SZ_OK)
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, 0, "szip_filter: decompression failed")
        assert(size_out==nalloc);

        /* Free the input buffer */
        H5MM_xfree(*buf);

        /* Set return values */
        *buf = outbuf;
        outbuf = NULL;
        *buf_size = nalloc;
        ret_value = size_out;
    }
    /* Output; compress */
    else {
        unsigned char *dst = NULL;    /* Temporary pointer to new output buffer */

        /* Allocate space for the compressed buffer & header (assume data won't get bigger) */
        if(NULL == (dst = outbuf = (unsigned char *)H5Z_aligned_malloc(nbytes + 4, lib_data)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, 0, "unable to allocate szip destination buffer")

        /* Encode the uncompressed length */
        H5_CHECK_OVERFLOW(nbytes,size_t,uint32_t);
        UINT32ENCODE(dst,nbytes);

        /* Compress the buffer */
        size_out = nbytes;
        if(SZ_OK!= SZ_BufftoBuffCompress(dst, &size_out, *buf, nbytes, &sz_param))
	    HGOTO_ERROR(H5E_PLINE, H5E_CANTINIT, 0, "overflow")
        assert(size_out<=nbytes);

        /* Free the input buffer */
        H5MM_xfree(*buf);

        /* Set return values */
        *buf = outbuf;
        outbuf = NULL;
        *buf_size = nbytes+4;
        ret_value = size_out+4;
    }

done:
    if(outbuf)
        H5MM_xfree(outbuf);
    FUNC_LEAVE_NOAPI(ret_value)
}

#endif /* H5_HAVE_FILTER_SZIP */

