/******************************************************************************
 * $Id$
 *
 * Project:  ENVI .hdr Driver
 * Purpose:  Implementation of ENVI .hdr labelled raw raster support.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2002, Frank Warmerdam
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
 * $Log$
 * Revision 1.9  2003/02/17 21:30:02  dron
 * Write support via Create() method added.
 *
 * Revision 1.8  2003/02/13 14:57:08  warmerda
 * Corrected bug in north/south handling for UTM.
 * Fix submitted by Vladimir Slepnev.
 *
 * Revision 1.7  2003/01/27 14:21:28  warmerda
 * Fixed byte_order test.
 *
 * Revision 1.6  2003/01/24 15:05:01  warmerda
 * Fixed byte order detection bugs as per bugzilla 261.
 *
 * Revision 1.5  2003/01/24 15:00:07  warmerda
 * Added additional data types as per Bugzilla 262.
 *
 * Revision 1.4  2002/09/04 06:50:37  warmerda
 * avoid static driver pointers
 *
 * Revision 1.3  2002/06/12 21:12:25  warmerda
 * update to metadata based driver info
 *
 * Revision 1.2  2002/05/11 16:22:01  warmerda
 * Also search for .hdr appended to full filename, not just .hdr replacing
 * the existing extension - as per Bugzilla bug 127 from Alessandro Amici.
 *
 * Revision 1.1  2002/03/04 21:52:48  warmerda
 * New
 *
 */

#include "rawdataset.h"
#include "ogr_spatialref.h"
#include "cpl_string.h"

CPL_CVSID("$Id$");

CPL_C_START
void	GDALRegister_ENVI(void);
CPL_C_END

static int anUsgsEsriZones[] =
{
  101, 3101,
  102, 3126,
  201, 3151,
  202, 3176,
  203, 3201,
  301, 3226,
  302, 3251,
  401, 3276,
  402, 3301,
  403, 3326,
  404, 3351,
  405, 3376,
  406, 3401,
  407, 3426,
  501, 3451,
  502, 3476,
  503, 3501,
  600, 3526,
  700, 3551,
  901, 3601,
  902, 3626,
  903, 3576,
 1001, 3651,
 1002, 3676,
 1101, 3701,
 1102, 3726,
 1103, 3751,
 1201, 3776,
 1202, 3801,
 1301, 3826,
 1302, 3851,
 1401, 3876,
 1402, 3901,
 1501, 3926,
 1502, 3951,
 1601, 3976,
 1602, 4001,
 1701, 4026,
 1702, 4051,
 1703, 6426,
 1801, 4076,
 1802, 4101,
 1900, 4126,
 2001, 4151,
 2002, 4176,
 2101, 4201,
 2102, 4226,
 2103, 4251,
 2111, 6351,
 2112, 6376,
 2113, 6401,
 2201, 4276,
 2202, 4301,
 2203, 4326,
 2301, 4351,
 2302, 4376,
 2401, 4401,
 2402, 4426,
 2403, 4451,
 2500,    0,
 2501, 4476,
 2502, 4501,
 2503, 4526,
 2600,    0,
 2601, 4551,
 2602, 4576,
 2701, 4601,
 2702, 4626,
 2703, 4651,
 2800, 4676,
 2900, 4701,
 3001, 4726,
 3002, 4751,
 3003, 4776,
 3101, 4801,
 3102, 4826,
 3103, 4851,
 3104, 4876,
 3200, 4901,
 3301, 4926,
 3302, 4951,
 3401, 4976,
 3402, 5001,
 3501, 5026,
 3502, 5051,
 3601, 5076,
 3602, 5101,
 3701, 5126,
 3702, 5151,
 3800, 5176,
 3900,    0,
 3901, 5201,
 3902, 5226,
 4001, 5251,
 4002, 5276,
 4100, 5301,
 4201, 5326,
 4202, 5351,
 4203, 5376,
 4204, 5401,
 4205, 5426,
 4301, 5451,
 4302, 5476,
 4303, 5501,
 4400, 5526,
 4501, 5551,
 4502, 5576,
 4601, 5601,
 4602, 5626,
 4701, 5651,
 4702, 5676,
 4801, 5701,
 4802, 5726,
 4803, 5751,
 4901, 5776,
 4902, 5801,
 4903, 5826,
 4904, 5851,
 5001, 6101,
 5002, 6126,
 5003, 6151,
 5004, 6176,
 5005, 6201,
 5006, 6226,
 5007, 6251,
 5008, 6276,
 5009, 6301,
 5010, 6326,
 5101, 5876,
 5102, 5901,
 5103, 5926,
 5104, 5951,
 5105, 5976,
 5201, 6001,
 5200, 6026,
 5200, 6076,
 5201, 6051,
 5202, 6051,
 5300,    0, 
 5400,    0
};

/************************************************************************/
/*                           ESRIToUSGSZone()                           */
/*                                                                      */
/*      Convert ESRI style state plane zones to USGS style state        */
/*      plane zones.                                                    */
/************************************************************************/

static int ESRIToUSGSZone( int nESRIZone )

{
    int		nPairs = sizeof(anUsgsEsriZones) / (2*sizeof(int));
    int		i;
    
    for( i = 0; i < nPairs; i++ )
    {
        if( anUsgsEsriZones[i*2+1] == nESRIZone )
            return anUsgsEsriZones[i*2];
    }

    return 0;
}

/************************************************************************/
/* ==================================================================== */
/*				ENVIDataset				*/
/* ==================================================================== */
/************************************************************************/

class ENVIDataset : public RawDataset
{
    FILE	*fpImage;	// image data file.
    FILE	*fp;		// header file
    const char	*pszHDRFilename;

    int		bFoundMapinfo;

    double      adfGeoTransform[6];

    char	*pszProjection;

    char        **papszHeader;

    int         ReadHeader( FILE * );
    int         ProcessMapinfo( const char * );
    
    char        **SplitList( const char * );

  public:
    		ENVIDataset();
    	        ~ENVIDataset();

    virtual void    FlushCache( void );
    virtual CPLErr  GetGeoTransform( double * padfTransform );
    virtual CPLErr  SetGeoTransform( double * );
    virtual const char *GetProjectionRef(void);
    virtual CPLErr  SetProjection( const char * );

    static GDALDataset *Open( GDALOpenInfo * );
    static GDALDataset *Create( const char * pszFilename,
                                int nXSize, int nYSize, int nBands,
                                GDALDataType eType, char ** papszOptions );
};

/************************************************************************/
/*                            ENVIDataset()                             */
/************************************************************************/

ENVIDataset::ENVIDataset()
{
    fpImage = NULL;
    fp = NULL;
    pszHDRFilename = NULL;
    pszProjection = CPLStrdup("");

    papszHeader = NULL;

    bFoundMapinfo = FALSE;

    adfGeoTransform[0] = 0.0;
    adfGeoTransform[1] = 1.0;
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = 0.0;
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = 1.0;
}

/************************************************************************/
/*                            ~ENVIDataset()                            */
/************************************************************************/

ENVIDataset::~ENVIDataset()

{
    FlushCache();
    if( fpImage )
        VSIFCloseL( fpImage );
    if( fp )
        VSIFClose( fp );
    if ( pszProjection )
	CPLFree( pszProjection );
    if ( papszHeader )
	CSLDestroy( papszHeader );
}

/************************************************************************/
/*                             FlushCache()                             */
/************************************************************************/

void ENVIDataset::FlushCache()

{
    GDALDataset::FlushCache();

    VSIFSeek( fp, 0, SEEK_END );

/* -------------------------------------------------------------------- */
/*      Write the rest of header.                                       */
/* -------------------------------------------------------------------- */
/* FIXME: write out Mapinfo projection */
    /*
    if ( pszProjection && !EQUAL(pszProjection, "") )
    {
	OGRSpatialReference oSRS;
	oSRS.importFromWkt( &pszProjection );

	VSIFPrintf( fp, "map info = {%s, 1, 1, %f, %f, %f, %f, %f, %s}\n",
		    oSRS.GetAttrValue("PROJECTION"), adfGeoTransform[0],
		    adfGeoTransform[3], adfGeoTransform[1], adfGeoTransform[5], oSRS.GetUTMZone(), "North");
    }*/

    VSIFPrintf( fp, "band names = {\n" );
    for ( int i = 1; i <= nBands; i++ )
    {
	VSIFPrintf( fp, "%s", GetRasterBand( i )->GetDescription() );
	if ( i != nBands )
	    VSIFPrintf( fp, ",\n" );
    }
    VSIFPrintf( fp, "}\n" );
}

/************************************************************************/
/*                          GetProjectionRef()                          */
/************************************************************************/

const char *ENVIDataset::GetProjectionRef()

{
    return pszProjection;
}

/************************************************************************/
/*                          SetProjection()                             */
/************************************************************************/

CPLErr ENVIDataset::SetProjection( const char *pszNewProjection )

{
    if ( pszProjection )
	CPLFree( pszProjection );
    pszProjection = CPLStrdup( pszNewProjection );

    return CE_None;
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr ENVIDataset::GetGeoTransform( double * padfTransform )

{
    memcpy( padfTransform, adfGeoTransform, sizeof(double) * 6 );
    
    if( bFoundMapinfo )
        return CE_None;
    else
        return CE_Failure;
}

/************************************************************************/
/*                          SetGeoTransform()                           */
/************************************************************************/

CPLErr ENVIDataset::SetGeoTransform( double * padfTransform )
{
    memcpy( adfGeoTransform, padfTransform, sizeof(double) * 6 );
    
    return CE_None;
}

/************************************************************************/
/*                             SplitList()                              */
/*                                                                      */
/*      Split an ENVI value list into component fields, and strip       */
/*      white space.                                                    */
/************************************************************************/

char **ENVIDataset::SplitList( const char *pszCleanInput )

{
    char	**papszReturn = NULL;
    char	*pszInput = CPLStrdup(pszCleanInput);

    if( pszInput[0] != '{' )
        return NULL;

    int iChar=1;


    while( pszInput[iChar] != '}' && pszInput[iChar] != '\0' )
    {
        int iFStart=-1, iFEnd=-1;

        // Find start of token.
        iFStart = iChar;
        while( pszInput[iFStart] == ' ' )
            iFStart++;

        iFEnd = iFStart;
        while( pszInput[iFEnd] != ',' 
               && pszInput[iFEnd] != '}'
               && pszInput[iFEnd] != '\0' )
            iFEnd++;

        if( pszInput[iFEnd] == '\0' )
            break;

        iChar = iFEnd+1;
        iFEnd = iFEnd-1;

        while( iFEnd > iFStart && pszInput[iFEnd] == ' ' )
            iFEnd--;

        pszInput[iFEnd+1] = '\0';
        papszReturn = CSLAddString( papszReturn, pszInput + iFStart );
    }

    CPLFree( pszInput );

    return papszReturn;
}

/************************************************************************/
/*                           ProcessMapinfo()                           */
/*                                                                      */
/*      Extract projection, and geotransform from a mapinfo value in    */
/*      the header.                                                     */
/************************************************************************/

int ENVIDataset::ProcessMapinfo( const char *pszMapinfo )

{
    char	**papszFields;		
    int         nCount;
    OGRSpatialReference oSRS;

    papszFields = SplitList( pszMapinfo );
    nCount = CSLCount(papszFields);

    if( nCount < 7 )
    {
        CSLDestroy( papszFields );
        return FALSE;
    }

    adfGeoTransform[0] = atof(papszFields[3]);
    adfGeoTransform[1] = atof(papszFields[5]);
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = atof(papszFields[4]);
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = -atof(papszFields[6]);

    if( EQUALN(papszFields[0],"UTM",3) && nCount >= 9 )
    {
        oSRS.SetUTM( atoi(papszFields[7]), 
                     !EQUAL(papszFields[8],"South") );
        oSRS.SetWellKnownGeogCS( "WGS84" );
    }
    else if( EQUALN(papszFields[0],"State Plane (NAD 27)",19)
             && nCount >= 8 )
    {
        oSRS.SetStatePlane( ESRIToUSGSZone(atoi(papszFields[7])), FALSE );
    }
    else if( EQUALN(papszFields[0],"State Plane (NAD 83)",19) 
             && nCount >= 8 )
    {
        oSRS.SetStatePlane( ESRIToUSGSZone(atoi(papszFields[7])), TRUE );
    }

    if( oSRS.GetRoot() == NULL )
        oSRS.SetLocalCS( papszFields[0] );

    if( EQUAL(papszFields[nCount-1],"units=Feet") )
    {
        oSRS.SetLinearUnits( SRS_UL_US_FOOT, atof(SRS_UL_US_FOOT_CONV) );
    }

    if( oSRS.GetRoot() != NULL )
    {
        CPLFree( pszProjection );
        pszProjection = NULL;
        oSRS.exportToWkt( &pszProjection );
    }

    return TRUE;
}

/************************************************************************/
/*                             ReadHeader()                             */
/************************************************************************/

int ENVIDataset::ReadHeader( FILE * fpHdr )

{
    char	szTestHdr[4];
    
/* -------------------------------------------------------------------- */
/*      Check that the first line says "ENVI".                          */
/* -------------------------------------------------------------------- */
    if( VSIFRead( szTestHdr, 4, 1, fpHdr ) != 1 )
        return FALSE;

    if( strncmp(szTestHdr,"ENVI",4) != 0 )
        return FALSE;

    CPLReadLine( fpHdr );

/* -------------------------------------------------------------------- */
/*      Now start forming sets of name/value pairs.                     */
/* -------------------------------------------------------------------- */
    while( TRUE )
    {
        const char *pszNewLine;
        char       *pszWorkingLine;

        pszNewLine = CPLReadLine( fpHdr );
        if( pszNewLine == NULL )
            break;

        if( strstr(pszNewLine,"=") == NULL )
            continue;

        pszWorkingLine = CPLStrdup(pszNewLine);

        // Collect additional lines if we have open sqiggly bracket.
        if( strstr(pszWorkingLine,"{") != NULL 
            && strstr(pszWorkingLine,"}") == NULL )
        {
            do { 
                pszNewLine = CPLReadLine( fpHdr );
                if( pszNewLine )
                {
                    pszWorkingLine = (char *) 
                        CPLRealloc(pszWorkingLine, 
                                 strlen(pszWorkingLine)+strlen(pszNewLine)+1);
                    strcat( pszWorkingLine, pszNewLine );
                }
            } while( pszNewLine != NULL && strstr(pszNewLine,"}") == NULL );
        }

        // Try to break input into name and value portions.  Trim whitespace.
        const char *pszValue;
        int         iEqual;

        for( iEqual = 0; 
             pszWorkingLine[iEqual] != '\0' && pszWorkingLine[iEqual] != '=';
             iEqual++ ) {}

        if( pszWorkingLine[iEqual] == '=' )
        {
            int		i;

            pszValue = pszWorkingLine + iEqual + 1;
            while( *pszValue == ' ' )
                pszValue++;
            
            pszWorkingLine[iEqual--] = '\0';
            while( iEqual > 0 && pszWorkingLine[iEqual] == ' ' )
                pszWorkingLine[iEqual--] = '\0';

            // Convert spaces in the name to underscores.
            for( i = 0; pszWorkingLine[i] != '\0'; i++ )
            {
                if( pszWorkingLine[i] == ' ' )
                    pszWorkingLine[i] = '_';
            }

            papszHeader = CSLSetNameValue( papszHeader, 
                                           pszWorkingLine, pszValue );
        }

        CPLFree( pszWorkingLine );
    }

    return TRUE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *ENVIDataset::Open( GDALOpenInfo * poOpenInfo )

{
    int		i;
    
/* -------------------------------------------------------------------- */
/*	We assume the user is pointing to the binary (ie. .bil) file.	*/
/* -------------------------------------------------------------------- */
    if( poOpenInfo->fp == NULL )
        return NULL;

/* -------------------------------------------------------------------- */
/*      Do we have a .hdr file?  Try upper and lower case, and          */
/*      replacing the extension as well as appending the extension      */
/*      to whatever we currently have.                                  */
/* -------------------------------------------------------------------- */
    const char	*pszMode;
    const char	*pszHdrFilename;
    FILE	*fpHeader;

    if( poOpenInfo->eAccess == GA_Update )
	pszMode = "r+";
    else
	pszMode = "r";

    pszHdrFilename = CPLResetExtension( poOpenInfo->pszFilename, "hdr" );
    fpHeader = VSIFOpen( pszHdrFilename, pszMode );

#ifndef WIN32
    if( fpHeader == NULL )
    {
        pszHdrFilename = CPLResetExtension( poOpenInfo->pszFilename, "HDR" );
        fpHeader = VSIFOpen( pszHdrFilename, pszMode );
    }
#endif
    if( fpHeader == NULL )
    {
        pszHdrFilename = CPLFormFilename( NULL, poOpenInfo->pszFilename, 
                                          "hdr" );
        fpHeader = VSIFOpen( pszHdrFilename, pszMode );
    }
#ifndef WIN32
    if( fpHeader == NULL )
    {
        pszHdrFilename = CPLFormFilename( NULL, poOpenInfo->pszFilename, 
                                          "HDR" );
        fpHeader = VSIFOpen( pszHdrFilename, pszMode );
    }
#endif

    if( fpHeader == NULL )
        return NULL;

/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    ENVIDataset 	*poDS;

    poDS = new ENVIDataset();
    poDS->pszHDRFilename = pszHdrFilename;
    poDS->fp = fpHeader;

/* -------------------------------------------------------------------- */
/*      Read the header.                                                */
/* -------------------------------------------------------------------- */
    if( !poDS->ReadHeader( fpHeader ) )
    {
        delete poDS;
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Has the user selected the .hdr file to open?                    */
/* -------------------------------------------------------------------- */
    if( EQUAL(CPLGetExtension(poOpenInfo->pszFilename), "hdr") )
    {
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "The selected file is an ENVI header file, but to\n"
                  "open ENVI datasets, the data file should be selected\n"
                  "instead of the .hdr file.  Please try again selecting\n"
                  "the data file corresponding to the header file:\n"
                  "  %s\n", 
                  poOpenInfo->pszFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Extract required values from the .hdr.                          */
/* -------------------------------------------------------------------- */
    int	nLines = 0, nSamples = 0, nBands = 0, nHeaderSize = 0;
    const char   *pszInterleave = NULL;

    if( CSLFetchNameValue(poDS->papszHeader,"lines") )
        nLines = atoi(CSLFetchNameValue(poDS->papszHeader,"lines"));

    if( CSLFetchNameValue(poDS->papszHeader,"samples") )
        nSamples = atoi(CSLFetchNameValue(poDS->papszHeader,"samples"));

    if( CSLFetchNameValue(poDS->papszHeader,"bands") )
        nBands = atoi(CSLFetchNameValue(poDS->papszHeader,"bands"));

    pszInterleave = CSLFetchNameValue(poDS->papszHeader,"interleave");

    if( nLines == 0 || nSamples == 0 || nBands == 0 || pszInterleave == NULL )
    {
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "The file appears to have an associated ENVI header, but\n"
                  "one or more of the samples, lines, bands and interleave\n"
                  "keywords appears to be missing." );
        return NULL;
    }

    if( CSLFetchNameValue(poDS->papszHeader,"header_offset") )
        nHeaderSize = atoi(CSLFetchNameValue(poDS->papszHeader,"header_offset"));

/* -------------------------------------------------------------------- */
/*      Translate the datatype.                                         */
/* -------------------------------------------------------------------- */
    GDALDataType	eType = GDT_Byte;

    if( CSLFetchNameValue(poDS->papszHeader,"data_type" ) != NULL )
    {
        switch( atoi(CSLFetchNameValue(poDS->papszHeader,"data_type" )) )
        {
          case 1:
            eType = GDT_Byte;
            break;

          case 2:
            eType = GDT_Int16;
            break;

          case 3:
            eType = GDT_Int32;
            break;

          case 4:
            eType = GDT_Float32;
            break;

          case 5:
            eType = GDT_Float64;
            break;

          case 6:
            eType = GDT_CFloat32;
            break;

          case 9:
            eType = GDT_CFloat64;
            break;

          case 12:
            eType = GDT_UInt16;
            break;

          case 13:
            eType = GDT_UInt32;
            break;

            /* 14=Int64, 15=UInt64 */

          default:
            CPLError( CE_Failure, CPLE_AppDefined, 
                      "The file has a 'data type' value of '%s'.  This value\n"
                      "isn't recognised by the GDAL ENVI driver.",
                      CSLFetchNameValue(poDS->papszHeader,"data_type" ) );
            return NULL;
        }
    }

/* -------------------------------------------------------------------- */
/*      Translate the byte order.					*/
/* -------------------------------------------------------------------- */
    int		bNativeOrder = TRUE;

    if( CSLFetchNameValue(poDS->papszHeader,"byte_order" ) != NULL )
    {
#ifdef CPL_LSB                               
        bNativeOrder = atoi(CSLFetchNameValue(poDS->papszHeader,
                                              "byte_order" )) == 0;
#else
        bNativeOrder = atoi(CSLFetchNameValue(poDS->papszHeader,
                                              "byte_order" )) != 0;
#endif
    }

/* -------------------------------------------------------------------- */
/*      Capture some information from the file that is of interest.     */
/* -------------------------------------------------------------------- */
    poDS->nRasterXSize = nSamples;
    poDS->nRasterYSize = nLines;
    poDS->eAccess = poOpenInfo->eAccess;

/* -------------------------------------------------------------------- */
/*      Reopen file in update mode if necessary.                        */
/* -------------------------------------------------------------------- */
    VSIFClose( poOpenInfo->fp );
    poOpenInfo->fp = NULL;

    if( poOpenInfo->eAccess == GA_Update )
        poDS->fpImage = VSIFOpenL( poOpenInfo->pszFilename, "rb+" );
    else
        poDS->fpImage = VSIFOpenL( poOpenInfo->pszFilename, "rb" );

    if( poDS->fpImage == NULL )
    {
        CPLError( CE_Failure, CPLE_OpenFailed, 
                  "Failed to re-open %s within ENVI driver.\n", 
                  poOpenInfo->pszFilename );
	delete poDS;
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Compute the line offset.                                        */
/* -------------------------------------------------------------------- */
    int	nDataSize = GDALGetDataTypeSize(eType)/8;
    int nPixelOffset, nLineOffset;
    vsi_l_offset nBandOffset;
    
    if( EQUAL(pszInterleave,"bsq") )
    {
        nLineOffset = nDataSize * nSamples;
        nPixelOffset = nDataSize;
        nBandOffset = nLineOffset * nLines;
    }
    else if( EQUAL(pszInterleave,"bil") )
    {
        nLineOffset = nDataSize * nSamples * nBands;
        nPixelOffset = nDataSize;
        nBandOffset = nDataSize * nSamples;
    }
    else if( EQUAL(pszInterleave,"bip") )
    {
        nLineOffset = nDataSize * nSamples * nBands;
        nPixelOffset = nDataSize * nBands;
        nBandOffset = nDataSize;
    }
    else
    {
        CPLError( CE_Failure, CPLE_AppDefined, 
                  "The interleaving type of the file (%s) is not supported.",
                  pszInterleave );
        return NULL;
    }
    
/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    poDS->nBands = nBands;
    for( i = 0; i < poDS->nBands; i++ )
    {
        poDS->SetBand( i + 1,
            new RawRasterBand(poDS, i + 1, poDS->fpImage,
                              nHeaderSize + nBandOffset * i,
                              nPixelOffset, nLineOffset, eType, bNativeOrder) );
    }

/* -------------------------------------------------------------------- */
/*      Apply band names if we have them.                               */
/* -------------------------------------------------------------------- */
    if( CSLFetchNameValue( poDS->papszHeader, "band_names" ) != NULL )
    {
        char	**papszBandNames = 
            poDS->SplitList( CSLFetchNameValue( poDS->papszHeader, 
                                                "band_names" ) );

        for( i = 0; i < MIN(CSLCount(papszBandNames),nBands); i++ )
            poDS->GetRasterBand(i + 1)->SetDescription( papszBandNames[i] );
    }
    
    
/* -------------------------------------------------------------------- */
/*      Look for mapinfo						*/
/* -------------------------------------------------------------------- */
    if( CSLFetchNameValue( poDS->papszHeader, "map_info" ) != NULL )
    {
        poDS->bFoundMapinfo = 
            poDS->ProcessMapinfo( 
                CSLFetchNameValue(poDS->papszHeader,"map_info") );
    }
    
/* -------------------------------------------------------------------- */
/*      Check for overviews.                                            */
/* -------------------------------------------------------------------- */
    poDS->oOvManager.Initialize( poDS, poOpenInfo->pszFilename );

    return( poDS );
}

/************************************************************************/
/*                               Create()                               */
/************************************************************************/

GDALDataset *ENVIDataset::Create( const char * pszFilename,
                                  int nXSize, int nYSize, int nBands,
                                  GDALDataType eType,
                                  char ** /* papszOptions */ )

{
/* -------------------------------------------------------------------- */
/*      Verify input options.                                           */
/* -------------------------------------------------------------------- */
    int		iENVIType;

    switch( eType )
    {
      case GDT_Byte:
	iENVIType = 1;
	break;
      case GDT_Int16:
	iENVIType = 2;
	break;
      case GDT_Int32:
	iENVIType = 3;
	break;
      case GDT_Float32:
	iENVIType = 4;
	break;
      case GDT_Float64:
	iENVIType = 5;
	break;
      case GDT_CFloat32:
	iENVIType = 6;
	break;
      case GDT_CFloat64:
	iENVIType = 9;
	break;
      case GDT_UInt16:
	iENVIType = 12;
	break;
      case GDT_UInt32:
	iENVIType = 13;
	break;

	/* 14=Int64, 15=UInt64 */

      default:
        CPLError( CE_Failure, CPLE_AppDefined,
              "Attempt to create ENVI .hdr labelled dataset with an illegal\n"
              "data type (%s).\n",
              GDALGetDataTypeName(eType) );
	return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Try to create the file.                                         */
/* -------------------------------------------------------------------- */
    FILE	*fp;

    fp = VSIFOpen( pszFilename, "wb" );

    if( fp == NULL )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed.\n",
                  pszFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Just write out a couple of bytes to establish the binary        */
/*      file, and then close it.                                        */
/* -------------------------------------------------------------------- */
    VSIFWrite( (void *) "\0\0", 2, 1, fp );
    VSIFClose( fp );

/* -------------------------------------------------------------------- */
/*      Create the .hdr filename.                                       */
/* -------------------------------------------------------------------- */
    const char	*pszHDRFilename;

    pszHDRFilename = CPLResetExtension(pszFilename, "hdr" );

/* -------------------------------------------------------------------- */
/*      Open the file.                                                  */
/* -------------------------------------------------------------------- */
    fp = VSIFOpen( pszHDRFilename, "wt" );
    if( fp == NULL )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed.\n",
                  pszHDRFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Write out the header.                                           */
/* -------------------------------------------------------------------- */
    int		iBigEndian;
#ifdef CPL_LSB
    iBigEndian = 0;
#else
    iBigEndian = 1;
#endif

    VSIFPrintf( fp, "ENVI\n" );
    VSIFPrintf( fp, "samples = %d\nlines   = %d\nbands   = %d\n",
		nXSize, nYSize, nBands );
    VSIFPrintf( fp, "header offset = 0\nfile type = ENVI Standard\n" );
    VSIFPrintf( fp, "data type = %d\n", iENVIType );
    VSIFPrintf( fp, "interleave = bsq\n" );
    VSIFPrintf( fp, "byte order = %d\n", iBigEndian );

    VSIFClose( fp );

    return (GDALDataset *) GDALOpen( pszFilename, GA_Update );
}

/************************************************************************/
/*                         GDALRegister_ENVI()                          */
/************************************************************************/

void GDALRegister_ENVI()

{
    GDALDriver	*poDriver;

    if( GDALGetDriverByName( "ENVI" ) == NULL )
    {
        poDriver = new GDALDriver();
        
        poDriver->SetDescription( "ENVI" );
        poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, 
                                   "ENVI .hdr Labelled" );
        poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, 
                                   "frmt_various.html#ENVI" );
        poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "" );
        poDriver->SetMetadataItem( GDAL_DMD_CREATIONDATATYPES, 
                                   "Byte Int16 UInt16 Int32 UInt32 Float32"
				   "Float64 CInt16 CInt32 CFloat32 CFloat64" );

        poDriver->pfnOpen = ENVIDataset::Open;
        poDriver->pfnCreate = ENVIDataset::Create;

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}

