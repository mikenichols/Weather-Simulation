#include <iostream>
#include <shapefil.h>

using namespace std;

int main( int argc, char **argv )
{
	// make sure we got at least 1 arg
	if (argc < 2) {
		cerr << "Not enough args" << endl;
		return 1;
	}

	int nEntities;
	int shapeType;
	int totalParts = 0;
	double minCoords[4], maxCoords[4];
	
	cout << "Shapefile data" << endl;
	SHPHandle hSHP;
	hSHP = SHPOpen( argv[1], "rb" );
	SHPGetInfo( hSHP, &nEntities, &shapeType, minCoords, maxCoords );

	for ( int i = 0; i < nEntities; i++ )
	{
		SHPObject *sObj = SHPReadObject( hSHP, i );
		
		cout << "Entity = " << i <<
				", type = " << sObj->nSHPType << 
				", id = " << sObj->nShapeId << 
				", parts = " << sObj->nParts << endl;
		// part offsets
		for (int j = 0; j < sObj->nParts; j++) {
			printf("panPartStart[%d] = %d\n", j, sObj->panPartStart[j]);
		}
		cout << "vertex list = " << sObj->nVertices << endl;
		// the points are in the array indexed up to nVertices
		for (int j = 0; j < sObj->nVertices; j++) {
			printf("\tx[%d] = %f, y[%d] = %f\n", j, sObj->padfX[j], j, sObj->padfY[j]);
		}
			
		totalParts += sObj->nParts;
		
		SHPDestroyObject( sObj );
	}
	
	cout << "shape type     = " << shapeType << endl;
	cout << "shape entities = " << nEntities << endl;
	cout << "shape parts    = " << totalParts << endl << endl;
	
	SHPClose( hSHP );

	// if we were only given a .shp file
	if (argc < 3) {
		cerr << "Only 1 arg supplied. Exiting." << endl;
		return 1;
	}

	cout << "DBF file data" << endl;
    DBFHandle hDBF;
    hDBF = DBFOpen( argv[2], "rb" );

	int rC = DBFGetRecordCount(hDBF);
	cout << "record count = " << rC << endl;
	int fC = DBFGetFieldCount(hDBF);
	cout << "field count = " << fC << endl;

    for( int i = 0; i < fC; i++ )
    {
        DBFFieldType	eType;
        const char	 	*pszTypeName;
	    int		nWidth, nDecimals;
	    int		bHeader = 0;
	    int		bRaw = 0;
	    int		bMultiLine = 0;
	    char	szTitle[12];

        eType = DBFGetFieldInfo( hDBF, i, szTitle, &nWidth, &nDecimals );
        if( eType == FTString ) pszTypeName = "String";
        else if( eType == FTInteger ) pszTypeName = "Integer";
        else if( eType == FTDouble ) pszTypeName = "Double";
        else if( eType == FTInvalid ) pszTypeName = "Invalid";

        printf( "Field %d: Type=%s, Title=`%s', Width=%d, Decimals=%d\n",
                i, pszTypeName, szTitle, nWidth, nDecimals );
    }
    DBFClose(hDBF);

	return 0;
}
