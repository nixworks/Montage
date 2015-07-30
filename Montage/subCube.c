/* Module: subCube.c

Version  Developer        Date     Change
-------  ---------------  -------  -----------------------
1.0      John Good        15May15  Baseline code, based on subImage.c of that date.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include "fitsio.h"
#include "wcs.h"
#include "coord.h"

#include "montage.h"
#include "subCube.h"
#include "mNaN.h"

int debug = 1;

int isflat;

char content[128];
        

struct WorldCoor *montage_getFileInfo(fitsfile *infptr, char *header[], struct imageParams *params)
{
   struct WorldCoor *wcs;
   int status = 0;
   int i;

   if(fits_get_image_wcs_keys(infptr, header, &status))
      montage_printFitsError(status);

   if(fits_read_key_lng(infptr, "NAXIS", &params->naxis, (char *)NULL, &status))
      montage_printFitsError(status);
   
   if(fits_read_keys_lng(infptr, "NAXIS", 1, params->naxis, params->naxes, &params->nfound, &status))
      montage_printFitsError(status);
   
   if(params->naxis < 3)
      params->naxes[2] = 1;

   if(params->naxis < 4)
      params->naxes[3] = 1;

   if(debug)
   {
      for(i=0; i<4; ++i)
         printf("naxis%d = %ld\n",  i+1, params->naxes[i]);

      fflush(stdout);
   }

   /****************************************/
   /* Initialize the WCS transform library */
   /* and find the pixel location of the   */
   /* sky coordinate specified             */
   /****************************************/

   wcs = wcsinit(header[0]);

   params->isDSS = 0;
   if(wcs->prjcode == WCS_DSS)
      params->isDSS = 1;

   if(wcs == (struct WorldCoor *)NULL)
   {
      fprintf(fstatus, "[struct stat=\"ERROR\", msg=\"Output wcsinit() failed.\"]\n");
      fflush(stdout);
      exit(1);
   }

   /* Extract the CRPIX and (equivalent) CDELT values */
   /* from the WCS structure                          */

   params->crpix[0] = wcs->xrefpix;
   params->crpix[1] = wcs->yrefpix;

   if(params->isDSS)
   {
      params->cnpix[0] = wcs->x_pixel_offset;
      params->cnpix[1] = wcs->y_pixel_offset;
   }
   return wcs;
}


int montage_copyHeaderInfo(fitsfile *infptr, fitsfile *outfptr, struct imageParams *params)
{
   double tmp;
   int naxis2, naxis3, naxis4;
   int status = 0;
   
   if(fits_copy_header(infptr, outfptr, &status))
      montage_printFitsError(status);


   /**********************/
   /* Update header info */
   /**********************/

   if(fits_update_key_lng(outfptr, "BITPIX", -64,
                                  (char *)NULL, &status))
      montage_printFitsError(status);

   if(fits_update_key_lng(outfptr, "NAXIS", params->naxis,
                                  (char *)NULL, &status))
      montage_printFitsError(status);

   if(fits_update_key_lng(outfptr, "NAXIS1", params->nelements,
                                  (char *)NULL, &status))
      montage_printFitsError(status);

   naxis2 = params->jend - params->jbegin + 1;
   naxis3 = params->pend - params->pbegin + 1;
   naxis4 = params->naxes[3];

   if(fits_update_key_lng(outfptr, "NAXIS2", naxis2,
                                  (char *)NULL, &status))
      montage_printFitsError(status);

   if(params->isDSS)
   {
      tmp = params->cnpix[0] + params->ibegin - 1;

      if(fits_update_key_dbl(outfptr, "CNPIX1", tmp, -14,
                                     (char *)NULL, &status))
         montage_printFitsError(status);

      tmp = params->cnpix[1] + params->jbegin - 1;

      if(fits_update_key_dbl(outfptr, "CNPIX2", tmp, -14,
                                     (char *)NULL, &status))
         montage_printFitsError(status);
   }
   else
   {
      tmp = params->crpix[0] - params->ibegin + 1;

      if(fits_update_key_dbl(outfptr, "CRPIX1", tmp, -14,
                                     (char *)NULL, &status))
         montage_printFitsError(status);

      tmp = params->crpix[1] - params->jbegin + 1;

      if(fits_update_key_dbl(outfptr, "CRPIX2", tmp, -14,
                                     (char *)NULL, &status))
         montage_printFitsError(status);
   }

   if(params->naxis > 2)
   {
      if(fits_update_key_lng(outfptr, "NAXIS3", naxis3,
                                     (char *)NULL, &status))
         montage_printFitsError(status);
   }

   if(params->naxis > 3)
   {
      if(fits_update_key_lng(outfptr, "NAXIS4", naxis4,
                                     (char *)NULL, &status))
         montage_printFitsError(status);
   }

   if(debug)
   {
      printf("naxis1 -> %ld\n", params->nelements);
      printf("naxis2 -> %d\n",  naxis2);

      if(params->naxis > 2)
         printf("naxis3 -> %d\n",  naxis3);

      if(params->naxis > 3)
         printf("naxis4 -> %d\n",  naxis4);

      if(params->isDSS)
      {
         printf("cnpix1 -> %-g\n", params->cnpix[0]+params->ibegin-1);
         printf("cnpix2 -> %-g\n", params->cnpix[1]+params->jbegin-1);
      }
      else
      {
         printf("crpix1 -> %-g\n", params->crpix[0]-params->ibegin+1);
         printf("crpix2 -> %-g\n", params->crpix[1]-params->jbegin+1);
      }

      fflush(stdout);
   }

   return 0;
}


void montage_copyData(fitsfile *infptr, fitsfile *outfptr, struct imageParams *params)
{
   long    fpixel[4], fpixelo[4];
   int     i, j, nullcnt;
   int     j3, j4;
   int     status = 0;
   double *buffer, refval;


   /*************************************************/
   /* Make a NaN value to use checking blank pixels */
   /*************************************************/

   union
   {
      double d;
      char   c[8];
   }
   value;

   double nan;

   for(i=0; i<8; ++i)
      value.c[i] = 255;

   nan = value.d;


   fpixel[1] = params->jbegin;
   fpixel[2] = params->pbegin;

   buffer  = (double *)malloc(params->nelements * sizeof(double));

   fpixelo[1] = 1;

   isflat = 1;

   refval = nan;

   fpixel [0] = params->ibegin;
   fpixelo[0] = 1;

   for (j4=1; j4<=params->naxes[3]; ++j4)
   {
      fpixel [3] = j4;
      fpixelo[3] = j4;

      fpixelo[2] = 1;

      for (j3=params->pbegin; j3<=params->pend; ++j3)
      {
         fpixel[2] = j3;

         fpixelo[1] = 1;

         for (j=params->jbegin; j<=params->jend; ++j)
         {
            fpixel[1] = j;

            if(debug >= 2)
            {
               printf("Processing input image row %5d\n", j);
               fflush(stdout);
            }

            if(fits_read_pix(infptr, TDOUBLE, fpixel, params->nelements, NULL,
                             buffer, &nullcnt, &status))
               montage_printFitsError(status);

            for(i=0; i<params->nelements; ++i)
            {
               if(!mNaN(buffer[i]))
               {
                  if(mNaN(refval))
                     refval = buffer[i];

                  if(buffer[i] != refval)
                     isflat = 0;
               }
            }

            if (fits_write_pix(outfptr, TDOUBLE, fpixelo, params->nelements,
                               (void *)buffer, &status))
               montage_printFitsError(status);

            ++fpixelo[1];
         }

         ++fpixelo[2];
      }
   }


   free(buffer);

   if(isflat)
   {
      if(mNaN(refval))
         strcpy(content, "blank");
      else
         strcpy(content, "flat");
   }
   else
      strcpy(content, "normal");
}


void montage_dataRange(fitsfile *infptr, int *imin, int *imax, int *jmin, int *jmax)
{
   long    fpixel[4];
   long    naxis, naxes[10];
   int     i, j, nullcnt, nfound;
   int     j4, j3;
   double *buffer;

   int     status = 0;

   /*************************************************/
   /* Make a NaN value to use checking blank pixels */
   /*************************************************/

   union
   {
      double d;
      char   c[8];
   }
   value;

   double nan;

   for(i=0; i<8; ++i)
      value.c[i] = 255;

   nan = value.d;

   if(fits_read_key_lng(infptr, "NAXIS", &naxis, (char *)NULL, &status))
      montage_printFitsError(status);
   
   if(fits_read_keys_lng(infptr, "NAXIS", 1, naxis, naxes, &nfound, &status))
      montage_printFitsError(status);

   fpixel[0] = 1;
   fpixel[1] = 1;
   fpixel[2] = 1;
   fpixel[3] = 1;

   *imin =  1000000000;
   *imax = -1;
   *jmin =  1000000000;
   *jmax = -1;

   buffer  = (double *)malloc(naxes[0] * sizeof(double));

   for (j4=1; j4<=naxes[3]; ++j4)
   {
      for (j3=1; j3<=naxes[2]; ++j3)
      {
         for (j=1; j<=naxes[1]; ++j)
         {
            if(debug >= 2)
            {
               printf("Processing image row %5d\n", j);
               fflush(stdout);
            }

            if(fits_read_pix(infptr, TDOUBLE, fpixel, naxes[0], NULL,
                             buffer, &nullcnt, &status))
               montage_printFitsError(status);

            for(i=1; i<=naxes[0]; ++i)
            {
               if(!mNaN(buffer[i-1]))
               {
                  if(buffer[i-1] != nan)
                  {
                     if(i < *imin) *imin = i;
                     if(i > *imax) *imax = i;
                     if(j < *jmin) *jmin = j;
                     if(j > *jmax) *jmax = j;
                  }
               }
            }

            ++fpixel[1];
         }

         ++fpixel[2];
      }

      ++fpixel[3];
   }

   free(buffer);
}


/***********************************/
/*                                 */
/*  Print out FITS library errors  */
/*                                 */
/***********************************/

void montage_printFitsError(int status)
{
   char status_str[FLEN_STATUS];

   fits_get_errstatus(status, status_str);

   fprintf(fstatus, "[struct stat=\"ERROR\", flag=%d, msg=\"%s\"]\n", status, status_str);

   exit(1);
}