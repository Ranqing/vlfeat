/** @internal
 ** @file     sift.c
 ** @author   Andrea Vedaldi
 ** @brief    Scale Invariant Feature Transform (SIFT) - MEX
 **/

#include "mexutils.h"
#include <vl/mathop.h>
#include <vl/sift.h>

#include <math.h>
#include <assert.h>

/* option codes */
enum {
  opt_octaves = 0,
  opt_levels,
  opt_first_octave,
  opt_frames,
  opt_edge_thresh,
  opt_peak_thresh,
  opt_norm_thresh,
  opt_orientations,
  opt_verbose 
} ;

/* options */
uMexOption options [] = {
  {"Octaves",      1,   opt_octaves       },
  {"Levels",       1,   opt_levels        },
  {"FirstOctave",  1,   opt_first_octave  },
  {"Frames",       1,   opt_frames        },
  {"PeakThresh",   1,   opt_peak_thresh   },
  {"EdgeThresh",   1,   opt_edge_thresh   },
  {"NormThresh",   1,   opt_norm_thresh   },
  {"Orientations", 0,   opt_orientations  },
  {"Verbose",      0,   opt_verbose       },
  {0,              0,   0                 }
} ;

/** ------------------------------------------------------------------
 ** @internal
 ** @brief Transpose desriptor
 **
 ** @param dst destination buffer.
 ** @param src source buffer.
 **
 ** The function writes to @a dst the transpose of the SIFT descriptor
 ** @a src. The tranpsose is defined as the descriptor that one
 ** obtains from computing the normal descriptor on the transposed
 ** image.
 **/

VL_INLINE void 
transpose_descriptor (vl_sift_pix* dst, vl_sift_pix* src) 
{
  int const BO = 8 ;  /* number of orientation bins */
  int const BP = 4 ;  /* number of spatial bins     */
  int i, j, t ;
  
  for (j = 0 ; j < BP ; ++j) {
    int jp = BP - 1 - j ;
    for (i = 0 ; i < BP ; ++i) {
      int o  = BO * i + BP*BO * j  ;
      int op = BO * i + BP*BO * jp ;      
      dst [op] = src[o] ;      
      for (t = 1 ; t < BO ; ++t) 
        dst [BO - t + op] = src [t + o] ;
    }
  }
}

/** -------------------------------------------------------------------
 ** @internal
 ** @brief Ordering of tuples by increasing scale
 ** 
 ** @param a tuple.
 ** @param b tuble.
 **
 ** @return @c a[2] < b[2]
 **/

int
korder (void const* a, void const* b) {
  double x = ((double*) a) [2] - ((double*) b) [2] ;
  if (x < 0) return -1 ;
  if (x > 0) return +1 ;
  return 0 ;
}

/** ------------------------------------------------------------------
 ** @brief MEX entry point
 **/

void
mexFunction(int nout, mxArray *out[], 
            int nin, const mxArray *in[])
{
  enum {IN_I=0,IN_END} ;
  enum {OUT_FRAMES=0, OUT_DESCRIPTORS} ;

  int                verbose = 0 ;
  int                opt ;
  int                next = IN_END ;
  mxArray const     *optarg ;
     
  vl_sift_pix const *data ;
  int                M, N ;

  int                O     = - 1 ;
  int                S     =   3 ;
  int                o_min =   0 ; 

  double             edge_thresh = -1 ;
  double             peak_thresh = -1 ;
  double             norm_thresh = -1 ;

  mxArray           *ikeys_array = 0 ;
  double            *ikeys = 0 ;
  int                nikeys = -1 ;
  vl_bool            force_orientations = 0 ;

  VL_USE_MATLAB_ENV ;
 
  /* -----------------------------------------------------------------
   *                                               Check the arguments
   * -------------------------------------------------------------- */

  if (nin < 1) {
    mexErrMsgTxt("One argument required.") ;
  } else if (nout > 2) {
    mexErrMsgTxt("Too many output arguments.");
  }
  
  if (mxGetNumberOfDimensions (in[IN_I]) != 2              ||
      mxGetClassID            (in[IN_I]) != mxSINGLE_CLASS  ) {
    mexErrMsgTxt("I must be a matrix of class SINGLE") ;
  }
  
  data = (vl_sift_pix*) mxGetData (in[IN_I]) ;
  M    = mxGetM (in[IN_I]) ;
  N    = mxGetN (in[IN_I]) ;
  
  while ((opt = uNextOption(in, nin, options, &next, &optarg)) >= 0) {
    switch (opt) {

    case opt_verbose :
      ++ verbose ;
      break ;

    case opt_octaves :
      if (!uIsRealScalar(optarg) || (O = (int) *mxGetPr(optarg)) < 0) {
        mexErrMsgTxt("'Octaves' must be a positive integer.") ;
      }
      break ;
      
    case opt_levels :
      if (! uIsRealScalar(optarg) || (S = (int) *mxGetPr(optarg)) < 1) {
        mexErrMsgTxt("'Levels' must be a positive integer.") ;
      }
      break ;
      
    case opt_first_octave :
      if (!uIsRealScalar(optarg)) {
        mexErrMsgTxt("'FirstOctave' must be an integer") ;
      }
      o_min = (int) *mxGetPr(optarg) ;
      break ;

    case opt_edge_thresh :
      if (!uIsRealScalar(optarg) || (edge_thresh = *mxGetPr(optarg)) < 1) {
        mexErrMsgTxt("'EdgeThresh' must be not smaller than 1.") ;
      }
      break ;

    case opt_peak_thresh :
      if (!uIsRealScalar(optarg) || (peak_thresh = *mxGetPr(optarg)) < 0) {
        mexErrMsgTxt("'PeakThresh' must be a non-negative real.") ;
      }
      break ;

    case opt_norm_thresh :
      if (!uIsRealScalar(optarg) || (norm_thresh = *mxGetPr(optarg)) < 0) {
        mexErrMsgTxt("'NormThresh' must be a non-negative real.") ;
      }
      break ;

    case opt_frames :
      if (!uIsRealMatrix(optarg, 4, -1)) {
        mexErrMsgTxt("'Frames' must be a 4 x N matrix.x") ;
      }
      ikeys_array = mxDuplicateArray (optarg) ;
      nikeys      = mxGetN (optarg) ;
      ikeys       = mxGetPr (ikeys_array) ;
      qsort (ikeys, nikeys, 4 * sizeof(double), korder) ;
      break ;
      
    case opt_orientations :
      force_orientations = 1 ;
      break ;
      
    default :
      assert(0) ;
      break ;
    }
  }
  
  /* -----------------------------------------------------------------
   *                                                            Do job
   * -------------------------------------------------------------- */
  {
    VlSiftFilt        *filt ;    
    vl_bool            first ;
    double            *frames = 0 ;
    vl_uint8          *descr  = 0 ;
    int                nframes = 0, reserved = 0, i,j,q ;
    
    /* create a filter to process the image */
    filt = vl_sift_new (M, N, O, S, o_min) ;

    if (peak_thresh >= 0) vl_sift_set_peak_thresh (filt, peak_thresh) ;
    if (edge_thresh >= 0) vl_sift_set_edge_thresh (filt, edge_thresh) ;
    if (norm_thresh >= 0) vl_sift_set_norm_thresh (filt, norm_thresh) ;
    
    if (verbose) {    
      mexPrintf("siftmx: filter settings:\n") ;
      mexPrintf("siftmx:   octaves      (O)     = %d\n", 
                vl_sift_get_octave_num   (filt)) ;
      mexPrintf("siftmx:   levels       (S)     = %d\n",
                vl_sift_get_level_num    (filt)) ;
      mexPrintf("siftmx:   first octave (o_min) = %d\n", 
                vl_sift_get_octave_first (filt)) ;
      mexPrintf("siftmx:   edge thresh           = %g\n",
                vl_sift_get_edge_thresh   (filt)) ;
      mexPrintf("siftmx:   peak thresh           = %g\n",
                vl_sift_get_peak_thresh   (filt)) ;
      mexPrintf("siftmx:   norm thresh           = %g\n",
                vl_sift_get_norm_thresh   (filt)) ;
      mexPrintf((nikeys >= 0) ? 
                "siftmx: will source frames? yes (%d)\n" :
                "siftmx: will source frames? no\n", nikeys) ;
      mexPrintf("siftmx: will force orientations? %s\n",
                force_orientations ? "yes" : "no") ;      
    }
    
    /* ...............................................................
     *                                             Process each octave
     * ............................................................ */
    i     = 0 ;
    first = 1 ;
    while (true) {
      int                   err ;
      VlSiftKeypoint const *keys  = 0 ;
      int                   nkeys = 0 ;
      
      if (verbose) {
        mexPrintf ("siftmx: processing octave %d\n",
                   vl_sift_get_octave_index (filt)) ;
      }

      /* Calculate the GSS for the next octave .................... */
      if (first) {
        err   = vl_sift_process_first_octave (filt, data) ;
        first = 0 ;
      } else {
        err   = vl_sift_process_next_octave  (filt) ;
      }        

      if (err) break ;
      
      if (verbose > 1) {
        printf("siftmx: GSS octave %d computed\n",
               vl_sift_get_octave_index (filt));
      }

      /* Run detector ............................................. */
      if (nikeys < 0) {
        vl_sift_detect (filt) ;
        
        keys  = vl_sift_get_keypoints     (filt) ;
        nkeys = vl_sift_get_keypoints_num (filt) ;
        i     = 0 ;
        
        if (verbose > 1) {
          printf ("siftmx: detected %d (unoriented) keypoints\n", nkeys) ;
        }
      } else {
        nkeys = nikeys ;
      }

      /* For each keypoint ........................................ */
      for (; i < nkeys ; ++i) {
        double                angles [4] ;
        int                   nangles ;
        VlSiftKeypoint        ik ;
        VlSiftKeypoint const *k ;

        /* Obtain keypoint orientations ........................... */
        if (nikeys >= 0) {
          vl_sift_keypoint_init (filt, &ik, 
                                 ikeys [4 * i + 1] - 1,
                                 ikeys [4 * i + 0] - 1,
                                 ikeys [4 * i + 2]) ;
          
          if (ik.o != vl_sift_get_octave_index (filt)) {
            break ;
          }

          k = &ik ;
          
          /* optionally compute orientations too */
          if (force_orientations) {
            nangles = vl_sift_calc_keypoint_orientations 
              (filt, angles, k) ;            
          } else {
            angles [0] = VL_PI / 2 - ikeys [4 * i + 3] ;
            nangles    = 1 ;
          }
        } else {
          k = keys + i ;
          nangles = vl_sift_calc_keypoint_orientations 
            (filt, angles, k) ;
        }

        /* For each orientation ................................... */
        for (q = 0 ; q < nangles ; ++q) {
          vl_sift_pix  buf [128] ;
          vl_sift_pix rbuf [128] ;

          /* compute descriptor (if necessary) */
          if (nout > 1) {
            vl_sift_calc_keypoint_descriptor 
              (filt, buf, k, angles [q]) ;
            transpose_descriptor (rbuf, buf) ;
          }

          /* make enough room for all these keypoints and more */
          if (reserved < nframes + 1) {
            reserved += 2 * nkeys ;
            frames = mxRealloc (frames, 4 * sizeof(double) * reserved) ;
            if (nout > 1) {
              descr  = mxRealloc (descr,  128 * sizeof(double) * reserved) ;
            }
          }

          /* Save back with MATLAB conventions. Notice tha the input
           * image was the transpose of the actual image. */
          frames [4 * nframes + 0] = k -> y + 1 ;
          frames [4 * nframes + 1] = k -> x + 1 ;
          frames [4 * nframes + 2] = k -> sigma ;
          frames [4 * nframes + 3] = VL_PI / 2 - angles [q] ;
          
          if (nout > 1) {
            for (j = 0 ; j < 128 ; ++j) {
              double x = 512.0 * rbuf [j] ;
              x = (x < 255.0) ? x : 255.0 ;
              descr [128 * nframes + j] = (vl_uint8) (x) ;
            }
          }

          ++ nframes ;
        } /* next orientation */
      } /* next keypoint */
    } /* next octave */
    
    if (verbose) {
      mexPrintf ("siftmx: found %d keypoints\n", nframes) ;
    }

    /* ...............................................................
     *                                                       Save back
     * ............................................................ */

    {
      int dims [2] ;
      
      /* create an empty array */
      dims [0] = 0 ;
      dims [1] = 0 ;      
      out[OUT_FRAMES] = mxCreateNumericArray 
        (2, dims, mxDOUBLE_CLASS, mxREAL) ;

      /* set array content to be the frames buffer */
      dims [0] = 4 ;
      dims [1] = nframes ;
      mxSetDimensions (out[OUT_FRAMES], dims, 2) ;
      mxSetPr         (out[OUT_FRAMES], frames) ;
      
      if (nout > 1) {
        
        /* create an empty array */
        dims [0] = 0 ;
        dims [1] = 0 ;
        out[OUT_DESCRIPTORS]= mxCreateNumericArray 
          (2, dims, mxUINT8_CLASS,  mxREAL) ;
        
        /* set array content to be the descriptors buffer */
        dims [0] = 128 ;
        dims [1] = nframes ;
        mxSetDimensions (out[OUT_DESCRIPTORS], dims, 2) ;
        mxSetData       (out[OUT_DESCRIPTORS], descr) ;
      }
    }
    
    /* cleanup */
    vl_sift_delete (filt) ;
    
    if (ikeys_array) 
      mxDestroyArray(ikeys_array) ;

  } /* end: do job */
}
