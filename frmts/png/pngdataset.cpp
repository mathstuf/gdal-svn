/******************************************************************************
 * $Id$
 *
 * Project:  PNG Driver
 * Purpose:  Implement GDAL PNG Support
 * Author:   Frank Warmerdam, warmerda@home.com
 *
 ******************************************************************************
 * Copyright (c) 2000, Frank Warmerdam
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************
 *
 * ISSUES:
 *  o CollectMetadata() will only capture TEXT chunks before the image 
 *    data as the code is currently structured. 
 *  o Interlaced images are read entirely into memory for use.  This is
 *    bad for large images.
 *  o Image reading is always strictly sequential.  Reading backwards will
 *    cause the file to be rewound, and access started again from the
 *    beginning. 
 *  o 1, 2 and 4 bit data promoted to 8 bit. 
 *  o Transparency values not currently read and applied to palette.
 *  o 16 bit alpha values are not scaled by to eight bit. 
 *  o I should install setjmp()/longjmp() based error trapping for PNG calls.
 *    Currently a failure in png libraries will result in a complete
 *    application termination. 
 * 
 * $Log$
 * Revision 1.2  2000/04/28 20:56:01  warmerda
 * converted to use CreateCopy() instead of Create()
 *
 * Revision 1.1  2000/04/27 19:39:51  warmerda
 * New
 */

#include "gdal_priv.h"
#include "png.h"
#include "cpl_string.h"

static GDALDriver	*poPNGDriver = NULL;

CPL_C_START
void	GDALRegister_PNG(void);
CPL_C_END


/************************************************************************/
/* ==================================================================== */
/*				PNGDataset				*/
/* ==================================================================== */
/************************************************************************/

class PNGRasterBand;

class PNGDataset : public GDALDataset
{
    friend	PNGRasterBand;

    FILE        *fpImage;
    png_structp hPNG;
    png_infop   psPNGInfo;
    int         nBitDepth;
    int         nColorType; /* PNG_COLOR_TYPE_* */
    int         bInterlaced;

    int         nBufferStartLine;
    int         nBufferLines;
    int         nLastLineRead;
    GByte      *pabyBuffer;

    GDALColorTable *poColorTable;

    void        CollectMetadata();
    CPLErr      LoadScanline( int );
    void        Restart();

  public:
                 PNGDataset();
                 ~PNGDataset();

    static GDALDataset *Open( GDALOpenInfo * );
    static GDALDataset *Create( const char * pszFilename,
                                int nXSize, int nYSize, int nBands,
                                GDALDataType eType, char ** papszParmList );

    virtual void FlushCache( void );
};

/************************************************************************/
/* ==================================================================== */
/*                            PNGRasterBand                             */
/* ==================================================================== */
/************************************************************************/

class PNGRasterBand : public GDALRasterBand
{
    friend	PNGDataset;

  public:

                   PNGRasterBand( PNGDataset *, int );

    virtual CPLErr IReadBlock( int, int, void * );
    virtual CPLErr IWriteBlock( int, int, void * );

    virtual GDALColorInterp GetColorInterpretation();
    virtual GDALColorTable *GetColorTable();
};


/************************************************************************/
/*                           PNGRasterBand()                            */
/************************************************************************/

PNGRasterBand::PNGRasterBand( PNGDataset *poDS, int nBand )

{
    this->poDS = poDS;
    this->nBand = nBand;

    if( poDS->nBitDepth == 16 )
        eDataType = GDT_UInt16;
    else
        eDataType = GDT_Byte;

    nBlockXSize = poDS->nRasterXSize;;
    nBlockYSize = 1;
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr PNGRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                  void * pImage )

{
    PNGDataset	*poGDS = (PNGDataset *) poDS;
    CPLErr      eErr;
    GByte       *pabyScanline;
    int         i, nPixelSize, nPixelOffset, nXSize = GetXSize();
    
    CPLAssert( nBlockXOff == 0 );

    if( poGDS->nBitDepth == 16 )
        nPixelSize = 2;
    else
        nPixelSize = 1;
    nPixelOffset = poGDS->nBands * nPixelSize;

/* -------------------------------------------------------------------- */
/*      Load the desired scanline into the working buffer.              */
/* -------------------------------------------------------------------- */
    eErr = poGDS->LoadScanline( nBlockYOff );
    if( eErr != CE_None )
        return eErr;

    pabyScanline = poGDS->pabyBuffer 
        + (nBlockYOff - poGDS->nBufferStartLine) * nPixelOffset * nXSize
        + nPixelSize * (nBand - 1);

/* -------------------------------------------------------------------- */
/*      Transfer between the working buffer the the callers buffer.     */
/* -------------------------------------------------------------------- */
    if( nPixelSize == nPixelOffset )
        memcpy( pImage, pabyScanline, nPixelSize * nXSize );
    else if( nPixelSize == 1 )
    {
        for( i = 0; i < nXSize; i++ )
            ((GByte *) pImage)[i] = pabyScanline[i*nPixelOffset];
    }
    else 
    {
        CPLAssert( nPixelSize == 2 );
        for( i = 0; i < nXSize; i++ )
        {
            ((GByte *) pImage)[i] = pabyScanline[i*nPixelOffset];
            ((GByte *) pImage)[i+1] = pabyScanline[i*nPixelOffset+1];
        }
    }

    return CE_None;
}

/************************************************************************/
/*                            IWriteBlock()                             */
/************************************************************************/

CPLErr PNGRasterBand::IWriteBlock( int nBlockXOff, int nBlockYOff,
                                   void * pImage )

{
    PNGDataset	*poGDS = (PNGDataset *) poDS;
    CPLErr      eErr;
    GByte       *pabyScanline;
    int         i, nPixelSize, nPixelOffset, nXSize = GetXSize();
    
    CPLAssert( nBlockXOff == 0 );
    
    eErr = poGDS->LoadScanline( nBlockYOff );
    if( eErr != CE_None )
        return eErr;

    if( poGDS->nBitDepth == 16 )
        nPixelSize = 2;
    else
        nPixelSize = 1;
    nPixelOffset = poGDS->nBands * nPixelSize;

    pabyScanline = poGDS->pabyBuffer 
        + (nBlockYOff - poGDS->nBufferStartLine) * nPixelOffset * nXSize
        + nPixelSize * (nBand - 1);

    if( nPixelSize == nPixelOffset )
        memcpy( pabyScanline, pImage, nPixelSize * nXSize );
    else if( nPixelSize == 1 )
    {
        for( i = 0; i < nXSize; i++ )
            pabyScanline[i*nPixelOffset] = ((GByte *) pImage)[i];
    }
    else 
    {
        CPLAssert( nPixelSize == 2 );
        for( i = 0; i < nXSize; i++ )
        {
            pabyScanline[i*nPixelOffset] = ((GByte *) pImage)[i];
            pabyScanline[i*nPixelOffset+1] = ((GByte *) pImage)[i+1];
        }
    }

    return CE_None;
}

/************************************************************************/
/*                       GetColorInterpretation()                       */
/************************************************************************/

GDALColorInterp PNGRasterBand::GetColorInterpretation()

{
    PNGDataset	*poGDS = (PNGDataset *) poDS;

    if( poGDS->nColorType == PNG_COLOR_TYPE_GRAY )
        return GCI_GrayIndex;

    else if( poGDS->nColorType == PNG_COLOR_TYPE_GRAY_ALPHA )
    {
        if( nBand == 1 )
            return GCI_GrayIndex;
        else
            return GCI_AlphaBand;
    }

    else  if( poGDS->nColorType == PNG_COLOR_TYPE_PALETTE )
        return GCI_PaletteIndex;

    else  if( poGDS->nColorType == PNG_COLOR_TYPE_RGB
              || poGDS->nColorType == PNG_COLOR_TYPE_RGB_ALPHA )
    {
        if( nBand == 1 )
            return GCI_RedBand;
        else if( nBand == 2 )
            return GCI_GreenBand;
        else if( nBand == 3 )
            return GCI_BlueBand;
        else 
            return GCI_AlphaBand;
    }
    else
        return GCI_GrayIndex;
}

/************************************************************************/
/*                           GetColorTable()                            */
/************************************************************************/

GDALColorTable *PNGRasterBand::GetColorTable()

{
    PNGDataset	*poGDS = (PNGDataset *) poDS;

    if( nBand == 1 )
        return poGDS->poColorTable;
    else
        return NULL;
}

/************************************************************************/
/* ==================================================================== */
/*                             PNGDataset                               */
/* ==================================================================== */
/************************************************************************/


/************************************************************************/
/*                            PNGDataset()                            */
/************************************************************************/

PNGDataset::PNGDataset()

{
    hPNG = NULL;
    psPNGInfo = NULL;
    pabyBuffer = NULL;
    nBufferStartLine = 0;
    nBufferLines = 0;
    nLastLineRead = -1;
    poColorTable = NULL;
}

/************************************************************************/
/*                           ~PNGDataset()                            */
/************************************************************************/

PNGDataset::~PNGDataset()

{
    FlushCache();

    png_destroy_read_struct( &hPNG, &psPNGInfo, NULL );

    VSIFClose( fpImage );

    if( poColorTable != NULL )
        delete poColorTable;
}

/************************************************************************/
/*                             FlushCache()                             */
/*                                                                      */
/*      We override this so we can also flush out local tiff strip      */
/*      cache if need be.                                               */
/************************************************************************/

void PNGDataset::FlushCache()

{
    GDALDataset::FlushCache();

    if( pabyBuffer != NULL )
    {
        CPLFree( pabyBuffer );
        pabyBuffer = NULL;
        nBufferStartLine = 0;
        nBufferLines = 0;
    }
}

/************************************************************************/
/*                              Restart()                               */
/*                                                                      */
/*      Restart reading from the beginning of the file.                 */
/************************************************************************/

void PNGDataset::Restart()

{
    png_destroy_read_struct( &hPNG, &psPNGInfo, NULL );

    VSIRewind( fpImage );

    hPNG = png_create_read_struct( PNG_LIBPNG_VER_STRING, this, NULL, NULL );

    psPNGInfo = png_create_info_struct( hPNG );

    png_init_io( hPNG, fpImage );
    png_read_info( hPNG, psPNGInfo );

    if( nBitDepth < 8 )
        png_set_packing( hPNG );

    nLastLineRead = -1;
}


/************************************************************************/
/*                            LoadScanline()                            */
/************************************************************************/

CPLErr PNGDataset::LoadScanline( int nLine )

{
    int   i;
    int   nPixelOffset;

    CPLAssert( nLine >= 0 && nLine < GetRasterYSize() );

    if( nLine >= nBufferStartLine && nLine < nBufferStartLine + nBufferLines)
        return CE_None;

    if( nBitDepth == 16 )
        nPixelOffset = 2 * GetRasterCount();
    else
        nPixelOffset = 1 * GetRasterCount();

/* -------------------------------------------------------------------- */
/*      If the file is interlaced, we will load the entire image        */
/*      into memory using the high level API.                           */
/* -------------------------------------------------------------------- */
    if( bInterlaced )
    {
        png_bytep	*png_rows;
        
        CPLAssert( pabyBuffer == NULL );

        if( nLastLineRead != -1 )
            Restart();

        nBufferStartLine = 0;
        nBufferLines = GetRasterYSize();
        pabyBuffer = (GByte *) 
            VSIMalloc(nPixelOffset*GetRasterXSize()*GetRasterYSize());
        
        if( pabyBuffer == NULL )
        {
            CPLError( CE_Failure, CPLE_OutOfMemory, 
                      "Unable to allocate buffer for whole interlaced PNG"
                      "image of size %dx%d.\n", 
                      GetRasterXSize(), GetRasterYSize() );
            return CE_Failure;
        }

        png_rows = (png_bytep*)CPLMalloc(sizeof(png_bytep) * GetRasterYSize());
        for( i = 0; i < GetRasterYSize(); i++ )
            png_rows[i] = pabyBuffer + i * nPixelOffset * GetRasterXSize();

        png_read_image( hPNG, png_rows );

        CPLFree( png_rows );

        nLastLineRead = GetRasterYSize() - 1;

        return CE_None;
    }

/* -------------------------------------------------------------------- */
/*      Ensure we have space allocated for one scanline                 */
/* -------------------------------------------------------------------- */
    if( pabyBuffer == NULL )
        pabyBuffer = (GByte *) CPLMalloc(nPixelOffset * GetRasterXSize());

/* -------------------------------------------------------------------- */
/*      Otherwise we just try to read the requested row.  Do we need    */
/*      to rewind and start over?                                       */
/* -------------------------------------------------------------------- */
    if( nLine <= nLastLineRead )
        Restart();

/* -------------------------------------------------------------------- */
/*      Read till we get the desired row.                               */
/* -------------------------------------------------------------------- */
    png_bytep      row;

    row = pabyBuffer;
    while( nLine > nLastLineRead )
    {
        png_read_rows( hPNG, &row, NULL, 1 );
        nLastLineRead++;
    }

    nBufferStartLine = nLine;
    nBufferLines = 1;

    return CE_None;
}

/************************************************************************/
/*                          CollectMetadata()                           */
/*                                                                      */
/*      We normally do this after reading up to the image, but be       */
/*      forwarned ... we can missing text chunks this way.              */
/*                                                                      */
/*      We turn each PNG text chunk into one metadata item.  It         */
/*      might be nice to preserve language information though we        */
/*      don't try to now.                                               */
/************************************************************************/

void PNGDataset::CollectMetadata()

{
    int   nTextCount;
    png_textp text_ptr;

    png_get_text( hPNG, psPNGInfo, &text_ptr, &nTextCount );

    for( int iText = 0; iText < nTextCount; iText++ )
    {
        char	*pszTag = CPLStrdup(text_ptr[iText].key);

        for( int i = 0; pszTag[i] != '\0'; i++ )
        {
            if( pszTag[i] == ' ' || pszTag[i] == '=' || pszTag[i] == ':' )
                pszTag[i] = '_';
        }

        SetMetadataItem( pszTag, text_ptr[iText].text );
        CPLFree( pszTag );
    }
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *PNGDataset::Open( GDALOpenInfo * poOpenInfo )

{
/* -------------------------------------------------------------------- */
/*	First we check to see if the file has the expected header	*/
/*	bytes.								*/    
/* -------------------------------------------------------------------- */
    if( poOpenInfo->nHeaderBytes < 4 )
        return NULL;

    if( png_sig_cmp(poOpenInfo->pabyHeader, (png_size_t)0, 
                    poOpenInfo->nHeaderBytes) != 0 )
        return NULL;

    if( poOpenInfo->eAccess == GA_Update )
    {
        CPLError( CE_Failure, CPLE_NotSupported, 
                  "The PNG driver does not support update access to existing"
                  " datasets.\n" );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    PNGDataset 	*poDS;

    poDS = new PNGDataset();

    poDS->eAccess = poOpenInfo->eAccess;
    
    poDS->hPNG = png_create_read_struct( PNG_LIBPNG_VER_STRING, poDS, 
                                         NULL, NULL );

    poDS->psPNGInfo = png_create_info_struct( poDS->hPNG );

/* -------------------------------------------------------------------- */
/*	Read pre-image data after ensuring the file is rewound.         */
/* -------------------------------------------------------------------- */
    /* we should likely do a setjmp() here */

    VSIRewind( poOpenInfo->fp );
    
    png_init_io( poDS->hPNG, poOpenInfo->fp );
    png_read_info( poDS->hPNG, poDS->psPNGInfo );

/* -------------------------------------------------------------------- */
/*      Capture some information from the file that is of interest.     */
/* -------------------------------------------------------------------- */
    poDS->nRasterXSize = png_get_image_width( poDS->hPNG, poDS->psPNGInfo );
    poDS->nRasterYSize = png_get_image_height( poDS->hPNG, poDS->psPNGInfo );

    poDS->nBands = png_get_channels( poDS->hPNG, poDS->psPNGInfo );
    poDS->nBitDepth = png_get_bit_depth( poDS->hPNG, poDS->psPNGInfo );
    poDS->bInterlaced = png_get_interlace_type( poDS->hPNG, poDS->psPNGInfo ) 
        				!= PNG_INTERLACE_NONE;

    poDS->nColorType = png_get_color_type( poDS->hPNG, poDS->psPNGInfo );
    
/* -------------------------------------------------------------------- */
/*      We want to treat 1,2,4 bit images as eight bit.  This call      */
/*      causes libpng to unpack the image.                              */
/* -------------------------------------------------------------------- */
    if( poDS->nBitDepth < 8 )
        png_set_packing( poDS->hPNG );

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    for( int iBand = 0; iBand < poDS->nBands; iBand++ )
        poDS->SetBand( iBand+1, new PNGRasterBand( poDS, iBand+1 ) );

/* -------------------------------------------------------------------- */
/*      Adopt the file pointer.                                         */
/* -------------------------------------------------------------------- */
    poDS->fpImage = poOpenInfo->fp;
    poOpenInfo->fp = NULL;

/* -------------------------------------------------------------------- */
/*      Is there a palette?  Note: we should also read back and         */
/*      apply transparency values if available.                         */
/* -------------------------------------------------------------------- */
    if( poDS->nColorType == PNG_COLOR_TYPE_PALETTE )
    {
        png_color *pasPNGPalette;
        int	nColorCount;
        GDALColorEntry oEntry;

        if( png_get_PLTE( poDS->hPNG, poDS->psPNGInfo, 
                          &pasPNGPalette, &nColorCount ) == 0 )
            nColorCount = 0;

        poDS->poColorTable = new GDALColorTable();

        for( int iColor = nColorCount - 1; iColor >= 0; iColor-- )
        {
            oEntry.c1 = pasPNGPalette[iColor].red;
            oEntry.c2 = pasPNGPalette[iColor].green;
            oEntry.c3 = pasPNGPalette[iColor].blue;
            oEntry.c4 = 255;

            poDS->poColorTable->SetColorEntry( iColor, &oEntry );
        }
    }

/* -------------------------------------------------------------------- */
/*      Extract any text chunks as "metadata".                          */
/* -------------------------------------------------------------------- */
    poDS->CollectMetadata();

/* -------------------------------------------------------------------- */
/*      Open overviews.                                                 */
/* -------------------------------------------------------------------- */
    poDS->oOvManager.Initialize( poDS, poOpenInfo->pszFilename );

    return poDS;
}

/************************************************************************/
/*                           PNGCreateCopy()                            */
/************************************************************************/

static GDALDataset *
PNGCreateCopy( const char * pszFilename, GDALDataset *poSrcDS, 
               int bStrict, char ** papszOptions, 
               GDALProgressFunc pfnProgress, void * pProgressData )

{
    int  nBands = poSrcDS->GetRasterCount();
    int  nXSize = poSrcDS->GetRasterXSize();
    int  nYSize = poSrcDS->GetRasterYSize();

/* -------------------------------------------------------------------- */
/*      Some some rudimentary checks                                    */
/* -------------------------------------------------------------------- */
    if( nBands != 1 && nBands != 2 && nBands != 3 && nBands != 4 )
    {
        CPLError( CE_Failure, CPLE_NotSupported, 
                  "PNG driver doesn't support %d bands.  Must be 1 (grey),\n"
                  "2 (grey+alpha), 3 (rgb) or 4 (rgba) bands.\n", 
                  nBands );

        return NULL;
    }

    if( poSrcDS->GetRasterBand(1)->GetRasterDataType() != GDT_Byte 
        && poSrcDS->GetRasterBand(1)->GetRasterDataType() != GDT_UInt16
        && bStrict )
    {
        CPLError( CE_Failure, CPLE_NotSupported, 
                  "PNG driver doesn't support data type %s. "
                  "Only eight and sixteen bit bands supported.\n", 
                  GDALGetDataTypeName( 
                      poSrcDS->GetRasterBand(1)->GetRasterDataType()) );

        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Setup some parameters.                                          */
/* -------------------------------------------------------------------- */
    int  nColorType, nBitDepth;
    GDALDataType eType;

    if( nBands == 1 )
        nColorType = PNG_COLOR_TYPE_GRAY;
    else if( nBands == 2 )
        nColorType = PNG_COLOR_TYPE_GRAY_ALPHA;
    else if( nBands == 3 )
        nColorType = PNG_COLOR_TYPE_RGB;
    else if( nBands == 4 )
        nColorType = PNG_COLOR_TYPE_RGB_ALPHA;

    if( poSrcDS->GetRasterBand(1)->GetRasterDataType() != GDT_UInt16 )
    {
        eType = GDT_Byte;
        nBitDepth = 8;
    }
    else 
    {
        eType = GDT_UInt16;
        nBitDepth = 16;
    }

/* -------------------------------------------------------------------- */
/*      Create the dataset.                                             */
/* -------------------------------------------------------------------- */
    FILE	*fpImage;

    fpImage = VSIFOpen( pszFilename, "wb" );
    if( fpImage == NULL )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, 
                  "Unable to create png file %s.\n", 
                  pszFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Initialize PNG access to the file.                              */
/* -------------------------------------------------------------------- */
    png_structp hPNG;
    png_infop   psPNGInfo;
    
    hPNG = png_create_write_struct( PNG_LIBPNG_VER_STRING, 
                                    NULL, NULL, NULL );
    psPNGInfo = png_create_info_struct( hPNG );
    
    png_init_io( hPNG, fpImage );

    png_set_IHDR( hPNG, psPNGInfo, nXSize, nYSize, 
                  nBitDepth, nColorType, PNG_INTERLACE_NONE, 
                  PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE );
    
    png_write_info( hPNG, psPNGInfo );

/* -------------------------------------------------------------------- */
/*      Loop over image, copying image data.                            */
/* -------------------------------------------------------------------- */
    GByte 	*pabyScanline;
    CPLErr      eErr;

    pabyScanline = (GByte *) CPLMalloc( nBands * nXSize * 2 );

    for( int iLine = 0; iLine < nYSize; iLine++ )
    {
        png_bytep       row = pabyScanline;

        for( int iBand = 0; iBand < nBands; iBand++ )
        {
            GDALRasterBand * poBand = poSrcDS->GetRasterBand( iBand+1 );
            eErr = poBand->RasterIO( GF_Read, 0, iLine, nXSize, 1, 
                                     pabyScanline + iBand, nXSize, 1, GDT_Byte,
                                     nBands, nBands * nXSize );
        }

        png_write_rows( hPNG, &row, 1 );
    }

    CPLFree( pabyScanline );

    png_write_end( hPNG, psPNGInfo );
    png_destroy_write_struct( &hPNG, &psPNGInfo );

    VSIFClose( fpImage );

    return (GDALDataset *) GDALOpen( pszFilename, GA_ReadOnly );
}

/************************************************************************/
/*                          GDALRegister_PNG()                        */
/************************************************************************/

void GDALRegister_PNG()

{
    GDALDriver	*poDriver;

    if( poPNGDriver == NULL )
    {
        poPNGDriver = poDriver = new GDALDriver();
        
        poDriver->pszShortName = "PNG";
        poDriver->pszLongName = "Portable Network Graphics";
        
        poDriver->pfnOpen = PNGDataset::Open;
        poDriver->pfnCreateCopy = PNGCreateCopy;

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}

