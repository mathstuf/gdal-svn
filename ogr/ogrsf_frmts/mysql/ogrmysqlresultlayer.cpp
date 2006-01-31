/******************************************************************************
 * $Id$
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Implements OGRMySQLResultLayer class.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2004, Frank Warmerdam <warmerdam@pobox.com>
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
 * Revision 1.5  2006/01/31 02:35:52  fwarmerdam
 * Fixed up missing SRS handling.
 *
 * Revision 1.4  2006/01/30 13:36:28  fwarmerdam
 * _NEWDECIMAL type missing on older systems
 *
 * Revision 1.3  2006/01/30 03:51:10  hobu
 * some god-awful hackery, but we can do a good job of reading field definitions from a select query as well as get a spatial reference.
 *
 * Revision 1.2  2005/09/21 01:00:01  fwarmerdam
 * fixup OGRFeatureDefn and OGRSpatialReference refcount handling
 *
 * Revision 1.1  2004/10/08 20:48:12  fwarmerdam
 * New
 *
 */

#include "cpl_conv.h"
#include "ogr_mysql.h"

CPL_CVSID("$Id$");

/************************************************************************/
/*                        OGRMySQLResultLayer()                         */
/************************************************************************/

OGRMySQLResultLayer::OGRMySQLResultLayer( OGRMySQLDataSource *poDSIn, 
                                          const char * pszRawQueryIn,
                                          MYSQL_RES *hResultSetIn )
{
    poDS = poDSIn;

    iNextShapeId = 0;

    pszRawStatement = CPLStrdup(pszRawQueryIn);

    hResultSet = hResultSetIn;

    BuildFullQueryStatement();

//    nFeatureCount = PQntuples(hInitialResultIn);

    poFeatureDefn = ReadResultDefinition();
}

/************************************************************************/
/*                        ~OGRMySQLResultLayer()                        */
/************************************************************************/

OGRMySQLResultLayer::~OGRMySQLResultLayer()

{
    CPLFree( pszRawStatement );
}

/************************************************************************/
/*                        ReadResultDefinition()                        */
/*                                                                      */
/*      Build a schema from the current resultset.                      */
/************************************************************************/

OGRFeatureDefn *OGRMySQLResultLayer::ReadResultDefinition()

{

/* -------------------------------------------------------------------- */
/*      Parse the returned table information.                           */
/* -------------------------------------------------------------------- */
    OGRFeatureDefn *poDefn = new OGRFeatureDefn( "sql_statement" );
    int            iRawField;

    poDefn->Reference();
    int width;
    int precision;
    char * pszGeomColumnTable;

    mysql_field_seek( hResultSet, 0 );
    for( iRawField = 0; 
         iRawField < (int) mysql_num_fields(hResultSet); 
         iRawField++ )
    {
        MYSQL_FIELD *psMSField = mysql_fetch_field( hResultSet );
        OGRFieldDefn    oField( psMSField->name, OFTString);

        switch( psMSField->type )
        {
          case FIELD_TYPE_TINY:
          case FIELD_TYPE_SHORT:
          case FIELD_TYPE_LONG:
          case FIELD_TYPE_INT24:
          case FIELD_TYPE_LONGLONG:
            oField.SetType( OFTInteger );
            width = (int)psMSField->length;
            oField.SetWidth(width);
            poDefn->AddFieldDefn( &oField );
            break;

          case FIELD_TYPE_DECIMAL:
#ifdef FIELD_TYPE_NEWDECIMAL
          case FIELD_TYPE_NEWDECIMAL:
#endif
            oField.SetType( OFTReal );
            
            // a bunch of hackery to munge the widths that MySQL gives 
            // us into corresponding widths and precisions for OGR
            precision =    (int)psMSField->decimals;
            width = (int)psMSField->length;
            if (!precision)
                width=width-1;
            width = width - precision;
            
            oField.SetWidth(width);
            oField.SetPrecision(precision);
            poDefn->AddFieldDefn( &oField );
            break;

          case FIELD_TYPE_FLOAT:
          case FIELD_TYPE_DOUBLE:
            width = (int)psMSField->length;
            oField.SetWidth(width);
            oField.SetType( OFTReal );
            poDefn->AddFieldDefn( &oField );
            break;


          case FIELD_TYPE_TIMESTAMP:
          case FIELD_TYPE_DATE:
          case FIELD_TYPE_TIME:
          case FIELD_TYPE_DATETIME:
          case FIELD_TYPE_YEAR:
          case FIELD_TYPE_STRING:
          case FIELD_TYPE_VAR_STRING:
            oField.SetType( OFTString );
            oField.SetWidth((int)psMSField->length);
            poDefn->AddFieldDefn( &oField );
            break;
            
          case FIELD_TYPE_BLOB:
            oField.SetType( OFTString );
            oField.SetWidth((int)psMSField->max_length);
            poDefn->AddFieldDefn( &oField );
            break;
                        
          case FIELD_TYPE_GEOMETRY:
            pszGeomColumnTable = CPLStrdup( psMSField->table);
            pszGeomColumn = CPLStrdup( psMSField->name);            
            break;
            
          default:
            // any other field we ignore. 
            break;
        }
        
        
        // assume a FID name first, and if it isn't there
        // take a field that is both not null and a primary key
        if( EQUAL(psMSField->name,"ogc_fid") )
        {
            bHasFid = TRUE;
            pszFIDColumn = CPLStrdup(oField.GetNameRef());
            continue;
        } else  
        if (IS_NOT_NULL(psMSField->flags) && IS_PRI_KEY(psMSField->flags))
        {
           bHasFid = TRUE;
           pszFIDColumn = CPLStrdup(oField.GetNameRef());
           continue;
        }
    }


    poDefn->SetGeomType( wkbNone );

    if (pszGeomColumn) 
    {
        char*        pszType=NULL;
        char         szCommand[1024];
        char           **papszRow;  
         
        // set to unknown first
        poDefn->SetGeomType( wkbUnknown );
        
        sprintf(szCommand, "SELECT type FROM geometry_columns WHERE f_table_name='%s'",
                pszGeomColumnTable );
 
    
        if( hResultSet != NULL )
            mysql_free_result( hResultSet );        

        if( !mysql_query( poDS->GetConn(), szCommand ) )
            hResultSet = mysql_store_result( poDS->GetConn() );

        papszRow = NULL;
        if( hResultSet != NULL )
            papszRow = mysql_fetch_row( hResultSet );


        if( papszRow != NULL && papszRow[0] != NULL )
        {
            pszType = papszRow[0];

            OGRwkbGeometryType nGeomType = wkbUnknown;
            

            // check only standard OGC geometry types
            if ( EQUAL(pszType, "POINT") )
                nGeomType = wkbPoint;
            else if ( EQUAL(pszType,"LINESTRING"))
                nGeomType = wkbLineString;
            else if ( EQUAL(pszType,"POLYGON"))
                nGeomType = wkbPolygon;
            else if ( EQUAL(pszType,"MULTIPOINT"))
                nGeomType = wkbMultiPoint;
            else if ( EQUAL(pszType,"MULTILINESTRING"))
                nGeomType = wkbMultiLineString;
            else if ( EQUAL(pszType,"MULTIPOLYGON"))
                nGeomType = wkbMultiPolygon;
            else if ( EQUAL(pszType,"GEOMETRYCOLLECTION"))
                nGeomType = wkbGeometryCollection;

            poDefn->SetGeomType( nGeomType );

        } 

        if( hResultSet != NULL )
            mysql_free_result( hResultSet );      
    
        sprintf( szCommand, 
                 "SELECT srid FROM geometry_columns "
                 "WHERE f_table_name = '%s'",
                 pszGeomColumnTable );

        if( !mysql_query( poDS->GetConn(), szCommand ) )
            hResultSet = mysql_store_result( poDS->GetConn() );

        papszRow = NULL;
        if( hResultSet != NULL )
            papszRow = mysql_fetch_row( hResultSet );
            

        if( papszRow != NULL && papszRow[0] != NULL )
        {
            nSRSId = atoi(papszRow[0]);
        }

        if( hResultSet != NULL )
            mysql_free_result( hResultSet );    
            
        sprintf( szCommand,
             "SELECT srtext FROM spatial_ref_sys WHERE srid = %d",
             nSRSId );
        
        if( !mysql_query( poDS->GetConn(), szCommand ) )
            hResultSet = mysql_store_result( poDS->GetConn() );
            
        char  *pszWKT = NULL;
        papszRow = NULL;
        if( hResultSet != NULL )
            papszRow = mysql_fetch_row( hResultSet );

        if( papszRow != NULL && papszRow[0] != NULL )
        {
            pszWKT =papszRow[0];
        }
        
        poSRS = new OGRSpatialReference();
        if( pszWKT == NULL || poSRS->importFromWkt( &pszWKT ) != OGRERR_NONE )
        {
            delete poSRS;
            poSRS = NULL;
        }
    
    } 


    return poDefn;
}

/************************************************************************/
/*                      BuildFullQueryStatement()                       */
/************************************************************************/

void OGRMySQLResultLayer::BuildFullQueryStatement()

{
    if( pszQueryStatement != NULL )
    {
        CPLFree( pszQueryStatement );
        pszQueryStatement = NULL;
    }

    /* Eventually we should consider trying to "insert" the spatial component
       of the query if possible within a SELECT, but for now we just use
       the raw query directly. */

    pszQueryStatement = CPLStrdup(pszRawStatement);
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRMySQLResultLayer::ResetReading()

{
    OGRMySQLLayer::ResetReading();
}

/************************************************************************/
/*                          GetFeatureCount()                           */
/************************************************************************/

int OGRMySQLResultLayer::GetFeatureCount( int bForce )

{
    // I wonder if we could do anything smart here...
    return OGRMySQLLayer::GetFeatureCount( bForce );
}

/************************************************************************/
/*                           GetSpatialRef()                            */
/*                                                                      */
/*      We override this to try and fetch the table SRID from the       */
/*      geometry_columns table if the srsid is -2 (meaning we           */
/*      haven't yet even looked for it).                                */
/************************************************************************/

OGRSpatialReference *OGRMySQLResultLayer::GetSpatialRef()

{

    return poSRS;

}

