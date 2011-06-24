/*
Weather Simulation
Written by Scott Friedman and Mike Nichols
UCLA Academic Technology Services

Movement keys:
UP = move up
DOWN = move down
LEFT = move left
RIGHT = move right
PAGE_UP = zoom in
PAGE_DOWN = zoom out

Function keys:
Numbers toggle weather attributes
1 = Snowpack
2 = Snowfall
3 = Precipitation
4 = Runoff

[ = Transparency down
] = Transparency up

T = toggles drawing of surface maps
D = toggles drawing of station data
L = toggles drawing of shape outlines for states/countries
*/

#include <iostream>
#include <stdlib.h>
#include <fstream>
#include <limits>
#include <glob.h>
#include <wordexp.h>
#include <string.h>
#include <netcdfcpp.h>
#include <vector>
#include <time.h>
#include <algorithm>
#include <math.h>

#define GL_GLEXT_PROTOTYPES
#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glut.h>

#include <libshp/shapefil.h>

#include <IL/il.h>
#define ILUT_USE_OPENGL
#include <IL/ilu.h>
#include <IL/ilut.h>

#define CORRECT_TEX_LOC

// uncomment the line below to launch the simulation in fullscreen mode
//#define FULLSCREEN

// uncomment the line below to turn on console output
#define CONSOLE_OUTPUT

// uncomment the line below to print draw time for shape data
//#define SHP_TIMING_ON

// uncomment the line below to use real z data for the csv coords
//#define DRAWING_3D

// uncomment the line below to sync changing of transparency between different attributes
// note: this does take an extra second
//#define CHANGE_ALL_TRANSPARENCIES_AT_ONCE

// uncomment the line below to show the calculation of slice endpoints
#define ANIMATE_SLICE_CHOP

// uncomment the line below to make negligible data transparent
#define HIDE_NEGLIGIBLE_DATA

// uncomment the line below to turn on slice graph color
//#define SLICE_COLOR_ON

#define DEBUG
#define DRAW_DEBUGX
//#define DEBUG2
//#define DEBUG_SLICE

// uncomment the line below to suppress error messages(NOT recommended)
//#define ERROR_NOTIFICATION_OFF

using namespace std;

extern "C" {
	extern void dgesv_(int *n, int *nrhs, double *a, int *lda, int *ipiv,
			double *b, int *ldb, int *info);
};

typedef struct {
	float x;
	float y;
	float z;
	int val;
} coord_t;

typedef struct {
	GLubyte R;
	GLubyte G;
	GLubyte B;
	float value;
} trans_t;

typedef enum {
	ATTR_MIN = 0,
	SNOWPACK = 0,
	SNOWFALL = 1,
	PRECIPITATION = 2,
	RUNOFF = 3,
	SNOWPACK_DAILY = 4,
	SNOWFALL_DAILY = 5,
	PRECIPITATION_DAILY = 6,
	RUNOFF_DAILY = 7,
	ATTR_MAX = 7,
} attribute_t;

typedef enum {
	TEXT_UP,
	TEXT_DOWN,
	TEXT_LOWER_LEFT,
} textpos_t;

typedef enum {
	TRANS_ONE = 1,
	TRANS_MANY = 4,
} transnum_t;

typedef enum {
	ABOVE_LINE,
	ON_LINE,
	BELOW_LINE,
} lineloc_t;

typedef enum {
	INSIDE,
	TOP_LEFT,
	TOP,
	TOP_RIGHT,
	LEFT,
	RIGHT,
	BOTTOM_LEFT,
	BOTTOM,
	BOTTOM_RIGHT,
} gridloc_t;

bool operator<(const trans_t& a, const trans_t& b) {
	return a.value < b.value;
}

// Global Constants
const float MAX_FLOAT = std::numeric_limits<float>::max();
const float MIN_FLOAT = std::numeric_limits<float>::min();
const int MAX_INT = std::numeric_limits<int>::max();
const float SLICE_GRAPH_WIDTH = 1000.0, SLICE_GRAPH_HEIGHT = 200.0;
// distance the text is drawn in front of "camera"
const float TEXT_DIST = 0.05;
// this the value which defines the limit of negligible values in the daily data
const float EPSILON = 1.0;
const int HOURS_PER_DAY = 24;
const GLubyte NEGLIGIBLE_TRANSPARENCY = 84;
const int SLICE_XAXIS_COORDS = 8;
void *BIG_FONT = GLUT_BITMAP_9_BY_15;
void *LITTLE_FONT = GLUT_BITMAP_HELVETICA_10;
// initialization vectors
vector<int> intVector;
vector<float> floatVector;
vector<GLuint> gluintVector;
vector< vector<int> > intVectorVector;
vector< vector<float> > floatVectorVector;

// Global Variables
// identifiers for the main and sub screen
int mainWindow = -1, sliceWindow = -1;
int screenWidth = 1280, screenHeight = 720;
int screenWidth2 = 0.65 * screenWidth, screenHeight2 = screenHeight / 4;

transnum_t transNum = TRANS_ONE;
// holds data imported from text file about weather transfer function
vector<trans_t> transFuncData;
// TODO: different transfer functions for each attribute
//vector< vector<trans_t> > transFuncData;

coord_t debugA, debugB, debugC, debugD;
coord_t debugX = {9001, 9001, 9001};
coord_t debugBox[4] = {debugX, debugX, debugX, debugX};
bool debugSlice = false;

float xMax = -MAX_FLOAT, xMin = MAX_FLOAT;
float yMax = -MAX_FLOAT, yMin = MAX_FLOAT;
float xMid, yMid;
float eye[3];
float eyeBase[2];
int imageNo = 0;
bool running = true;
bool saving = false;

coord_t lineStart, lineEnd;
coord_t screenStart, screenEnd;
bool drawingLine = false;
double dragStart[2], dragEnd[2];
bool draggingMap = false;
bool leftMouseIsDown = false;

// location for each weather station
vector<coord_t> csvCoords;
// outer layer is each time step(day)
// inner layer is each weather station
vector< vector<float> > csvData;
float csvMin = MAX_FLOAT, csvMax = -MAX_FLOAT;
bool shouldDrawStations = false;

GLubyte transparency;
textpos_t datePosition = TEXT_DOWN;

int weatherAttrNum;
float *weatherCoords = NULL;
float *weatherData = NULL;
float *snowpackData = NULL;
float *snowfallData = NULL;
float *precipitationData = NULL;
float *runoffData = NULL;

float weatherAttrMin[8] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, 
	MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
float weatherAttrMax[8] = {-MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT, 
	-MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT, -MAX_FLOAT};

// very important
int totalTimeSteps;
int currentTimeStep = 0;
long recSize, timeSize, totalSliceSteps;
int numCols, numRows, numNcFiles;

vector<coord_t> sliceLegendCoords;
gridloc_t startPos = INSIDE;
gridloc_t endPos = INSIDE;

GLuint *textures;
vector<coord_t> texCoords;
bool shouldDrawTextures = true;

double FOVY = 50.0;
// the .1615 was found empirically, because using the unmodified FOVY yielded incorrect results
double viewAngle = 90 - (FOVY / 2.0) - 0.1615;
double tanViewAngle = tan(viewAngle);

// 1-D array with X,Y coords interleaved
//vector<float> weatherCoords;
// outer layer is each pair of rows
vector< vector<GLuint> > weatherIndices;
vector<float> weatherOutline;
bool shouldDrawOutline = true;

/*
Each vector below represents data for all shapefiles
	shapeCoords holds all the (x,y) coordinates for all entities in all shapefiles
	partOffsets holds all the initial indices for each part of each entity for all shapefiles

Each entry vector[i] below holds all the data for one shapefile
	shapeCoords[0] holds all (x,y) coords for all parts of all entities for fileNum == 0
	partOffsets[0] holds all the initial indices for each part of each entity for fileNum == 0

Each entry vector[i][j] holds data for all the (x,y) coords of one entity in one file
	shapeCoords[0][2] holds all the (x,y) coords for entity == 2 in fileNum == 0
	partOffsets[3][50] holds all the initial indices for each part of entity == 50 in fileNum == 3

Each entry vector[i][j][k] holds one (x,y) coordinate for one entity in one shapefile
These coordinates are interleaved like this: (x0, y0, x1, y1, x2, y2 ...etc.)
	shapeCoords[3][1][4] holds one x coordinate for point == 4, entity == 1 in fileNum == 3
	shapeCoords[3][1][5] holds the corresponding y coordinate for that vertex
	partOffsets[1][9][5] holds the initial index for part == 5 of entity == 9 in fileNum == 1
	The indices this part would only go from partOffsets[1][9][5] to partOffsets[1][9][6] - 1
*/
vector< vector< vector<float> > > shapeCoords;
vector< vector< vector<int> > > partOffsets;
bool shouldDrawShapes = true;

// to make the compiler happy
void reshape(int w, int h);
void reshape2(int w, int h);
void zoom(int direction);
void move(char direction);
void animate(void);
void key(unsigned char key, int x, int y);
void specialKey(int key, int x, int y);
void motion(int x, int y);
void mouse(int button, int state, int x, int y);
void vis(int visible);
void redraw(void);
void redraw2(void);
void drawBitmapString(float x, float y, float z, void *font, char *string);
void drawTriangle(coord_t center, float size);
void drawX(coord_t center, float size);
void drawStations(int day);
void drawText(int totalDays);
void drawTransferLegend(void);
void drawColorbar(vector<trans_t> colors, vector<coord_t> coords, attribute_t type);
void drawShapedata(int fileNum);
coord_t screen2worldCoords(int screenX, int screenY, float worldZ);
coord_t points2vector(coord_t a, coord_t b);
double calcDistance(float x1, float y1, float x2, float y2);
void calcSliceSteps(void);
void solve4x4Lapack(double *a, double *b);
void interpolateSliceGraph(float *sliceData, int sdsize);
bool insideCell(float x, float y, int &i);
bool findFirstCell(float x, float y, int &index);
bool nextInterpolationPoint(float x, float y, int &index, int level);
int getShapeFileData(int fileNum, char *fileName);
void jpeg2texture(int texNum, char *imageName);
void parseImageLocation(char *fileName);
void computeColors(GLubyte *weatherColors, int wcsize, float *data, int dataSize);
void computeDailyColors(GLubyte *weatherColors, int wcsize, float *data, int dataSize);
void computeSliceCoords(float *sliceCoords, int cosize, float *sliceData, float *prevSliceData);
void allocateWeatherDataSpace(NcFile *ncF);
void precomputeWeatherParameters(NcFile *ncF);
void computeMaxsAndMins(void);
void parseCSVfiles(char *locFileName, char *dataFileName);
void parseTransferFile(char *fileName);
lineloc_t aboveOrBelowLine(coord_t a, coord_t b, coord_t c);
void setTrans(trans_t &t, GLubyte R, GLubyte G, GLubyte B);
void setTrans(trans_t &t, GLubyte R, GLubyte G, GLubyte B, float val);
void setCoord(coord_t &c, float x, float y, float z);
void setCoord(coord_t &c, float x, float y, float z, int val);
void unreachable(char *funcName);
void cleanUpMemory(void);

void reshape(int w, int h) {
	// prevent a divide by zero error
	if (h == 0) h = 1;

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(FOVY, (float)w / (float)h, 0.01f, 4000.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	//gluLookAt(xMid, yMid, 45.0, xMid, yMid, 0.0, 0.0, 1.0, 0.0);
	gluLookAt(eye[0], eye[1], eye[2], eye[0], eye[1], 0.0, 0.0, 1.0, 0.0);

	screenWidth = w;
	screenHeight = h;
	return;
}

void reshape2(int w, int h) {
	// prevent a divide by zero error
	if (h == 0) h = 1;

	float zoom = 1.58 * h;

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(FOVY, (float)w / (float)h, 0.01f, 4000.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	//gluLookAt(eye[0], eye[1], eye[2], eye[0], eye[1], 0.0, 0.0, 1.0, 0.0);
	gluLookAt(475.0, 80.0, zoom,
			  475.0, 80.0, 0.0,
			  0.0, 1.0, 0.0);

	screenWidth2 = w;
	screenHeight2 = h;
	return;
}

// zoom more slowly closer to the map
void zoom(int direction) {
	if (eye[2] > 10) eye[2] += direction;
	else {
		if (direction == -1) eye[2] *= 0.9;
		else eye[2] *= 1.1;
	}
	#ifdef DEBUG2
	cout << "zoom = " << eye[2] << endl;
	#endif
	return;
}

void move(char direction) {
	int index, amount;

	// up = 'u', down = 'd', left = 'l', right = 'r'
	if ((direction == 'u') || (direction == 'r')) amount = 1;
	else amount = -1;
	if ((direction == 'u') || (direction == 'd')) index = 1;
	else index = 0;

	// finer resolutions move less absolute distance
	if (eye[2] > 10) eye[index] += amount;
	else if (eye[2] > 1) eye[index] += amount / 10.0;
	else if (eye[2] > 0.1) eye[index] += amount / 100.0;
	else eye[index] += amount / 1000.0;
	return;
}

void animate(void) {
	if (running) {
		currentTimeStep++;
		// reset the currentTimeStep when it reaches the end
		if (currentTimeStep > totalTimeSteps) currentTimeStep = 0;
		#ifdef DEBUG2
		cout << "current time step: " << currentTimeStep << endl;
		if (currentTimeStep % 100 == 0) cout << currentTimeStep << endl;
		#endif
		// used to output a series of images that can be made into a movie
		if (saving) {
			// take a screen shot of the window
			ilutGLScreen();
			stringstream ss;
			ss << "image";
			ss.fill('0');
			ss.width(5);
			// below is equivalent to: ss = "imageXXXXX.png\0";
			ss << imageNo++ << ".png" << ends;
			#ifdef CONSOLE_OUTPUT
			printf("Saving %s\n", ss.str().c_str());
			#endif
			ilSave(IL_PNG, ss.str().c_str());
			// stop saving images if we've reached the end of the animation
			if (currentTimeStep < imageNo) saving = false;
		}
	}
	glutPostRedisplay();
	return;
}

void key(unsigned char key, int x, int y) {
	// escape key exits the program
	if (key == 27) exit(0);

	switch (key) {
		// 1-8 change the current weather attribute
		case '1':
			weatherAttrNum = SNOWPACK;
			weatherData = snowpackData;
			break;
		case '2':
			weatherAttrNum = SNOWFALL;
			weatherData = snowfallData;
			break;
		case '3':
			weatherAttrNum = PRECIPITATION;
			weatherData = precipitationData;
			break;
		case '4':
			weatherAttrNum = RUNOFF;
			weatherData = runoffData;
			break;
		case '5':
			weatherAttrNum = SNOWPACK_DAILY;
			weatherData = snowpackData;
			break;
		case '6':
			weatherAttrNum = SNOWFALL_DAILY;
			weatherData = snowfallData;
			break;
		case '7':
			weatherAttrNum = PRECIPITATION_DAILY;
			weatherData = precipitationData;
			break;
		case '8':
			weatherAttrNum = RUNOFF_DAILY;
			weatherData = runoffData;
			break;
		case 'd':
			shouldDrawStations = !shouldDrawStations;
			break;
		case 'i':
			saving = !saving;
			break;
		case 'l':
			shouldDrawShapes = !shouldDrawShapes;
			break;
		case 'o':
			shouldDrawOutline = !shouldDrawOutline;
			break;
		case 'p':
			if (datePosition == TEXT_LOWER_LEFT) datePosition = TEXT_UP;
			else if (datePosition == TEXT_UP) datePosition = TEXT_DOWN;
			else datePosition = TEXT_LOWER_LEFT;
			break;
		case 'r':
			currentTimeStep = 0;
			imageNo = 0;
			break;
		case 's':
			running = !running;
			#ifdef CONSOLE_OUTPUT
			if (running) cout << "Simulation resumed." << endl;
			else cout << "Simulation paused." << endl;
			#endif
			break;
		case 't':
			shouldDrawTextures = !shouldDrawTextures;
			break;
		case '[':
			// decrease transparency
			if (transparency >= 25) transparency -= 25;
			break;
		case ']':
			// increase transparency
			if (transparency <= 230) transparency += 25;
			break;
		// no default needed
	}
	glutPostRedisplay();
	return;
}

void specialKey(int key, int x, int y) {
	switch (key) {
		case GLUT_KEY_PAGE_UP:
			zoom(-1);
			break;
		case GLUT_KEY_PAGE_DOWN:
			zoom(1);
			break;
		case GLUT_KEY_UP:
			move('u');
			break;
		case GLUT_KEY_DOWN:
			move('d');
			break;
		case GLUT_KEY_LEFT:
			move('l');
			break;
		case GLUT_KEY_RIGHT:
			move('r');
			break;
	}

	reshape(screenWidth, screenHeight);
	glutPostRedisplay();
	return;
}

// this function is called whenever the mouse is moved while holding down a button
void motion(int x, int y) {
	#ifdef DEBUG2
	printf("motion(%d, %d)\n", x, y);
	#endif
	coord_t world;
	if (drawingLine) {
		// save the world coord
		world = screen2worldCoords(x, y, 0.0);
		lineEnd.x = world.x;
		lineEnd.y = world.y;

	}
	else if (draggingMap) {
		world = screen2worldCoords(x, y, 0.0);
		printf("wx = %f wy = %f\n", world.x, world.y);
		dragEnd[0] = world.x;
		dragEnd[1] = world.y;
		/*
		double diffX = world.x - dragStart[0];
		double diffY = world.y - dragStart[1];
		printf("eb0 = %f eb1 = %f\n", eyeBase[0], eyeBase[1]);
		eye[0] = eyeBase[0] - diffX;
		eye[1] = eyeBase[1] - diffY;
		reshape(screenWidth, screenHeight);
		// */
	}
	return;
}

void mouse(int button, int state, int x, int y) {
	#ifdef DEBUG2
	printf("mouse(%d, %d, %d, %d)\n", button, state, x, y);
	#endif
	coord_t world;

	if (button == GLUT_LEFT_BUTTON) {
		if (state == GLUT_DOWN) {
			drawingLine = true;

			// save the world coords
			world = screen2worldCoords(x, y, 0.0);
			lineStart.x = world.x;
			lineStart.y = world.y;

			// save the screen coords to find totalSliceSteps
			screenStart.x = x;
			screenStart.y = y;

			// don't draw a line when user first clicks
			lineEnd.x = world.x;
			lineEnd.y = world.y;

			/* hide the slice window if it exists
			if (sliceWindow != -1) {
				int win = glutGetWindow();
				glutSetWindow(sliceWindow);
				glutHideWindow();
				glutSetWindow(win);
			}
			// */
			// clear the slice points vector
			while (sliceLegendCoords.size() > 0) {
				sliceLegendCoords.pop_back();
			}
		}
		if (state == GLUT_UP) {
			// reset these values to their original states
			drawingLine = false;

			world = screen2worldCoords(x, y, 0.0);
			lineEnd.x = world.x;
			lineEnd.y = world.y;

			screenEnd.x = x;
			screenEnd.y = y;

			calcSliceSteps();

			// only do this once
			if (sliceWindow == -1) {
				glutInitWindowSize(screenWidth2, screenHeight2);
				sliceWindow = glutCreateWindow("Weather Slice");
				glutPositionWindow(0, screenHeight + 80);
				glutDisplayFunc(redraw2);
				glutReshapeFunc(reshape2);
			}
			/* show the window if it already exists
			else {
				int win = glutGetWindow();
				glutSetWindow(sliceWindow);
				glutShowWindow();
				glutPositionWindow(0, screenHeight + 80);
				glutSetWindow(win);
			}
			// */
		}
	}
	/*
	else if (button == GLUT_RIGHT_BUTTON) {
		if (state == GLUT_DOWN) {
			draggingMap = true;
			world = screen2worldCoords(x, y, 0.0);

			dragStart[0] = world.x;
			dragStart[1] = world.y;

			// save the current eye location
			eyeBase[0] = eye[0];
			eyeBase[1] = eye[1];
		}
		if (state == GLUT_UP) {
			draggingMap = false;
		}
	}
	// */
	else if (button == 3 && state == 0) {
		zoom(-1);
	}
	else if (button == 4 && state == 0) {
		zoom(1);
	}
	reshape(screenWidth, screenHeight);
	return;
}

void vis(int visible) {
	return;
}

void redraw(void) {
	// reset the current time step if we reach the end
	// TODO: this may be redundant
	if (currentTimeStep == numNcFiles * timeSize) currentTimeStep = 0;

	#ifdef DEBUG2
	printf("currentTimeStep = %d\n", currentTimeStep);
	#endif

	glutSetWindow(mainWindow);
	glDisable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// FIXME haven't really gotten map dragging to work, but it wasn't required
	if (draggingMap) {
		double diffX = dragStart[0] - dragEnd[0];
		double diffY = dragStart[1] - dragEnd[1];
		eye[0] = eyeBase[0] + diffX;
		eye[1] = eyeBase[1] - diffY;
	}

	// ****draw the map textures
	if (shouldDrawTextures) {
		// scaling factors
		float sizex = 40.0;
		float sizey = 50.0;

		for (int i = 0; i < texCoords.size(); i++) {
			float lowerx = texCoords[i].x;
			float lowery = texCoords[i].y;

			#ifdef CORRECT_TEX_LOC
			float correctionX = 0.03;
			float correctionY = 0.045;
			#else
			float correctionX = 0.0;
			float correctionY = 0.0;
			#endif
			
			glEnable(GL_TEXTURE_2D);

			// select which texture to render
			glBindTexture(GL_TEXTURE_2D, textures[i]);
			// draw the textures
			glBegin(GL_QUADS);
				// might put each of these statements on separate line
				glTexCoord2i(0, 0); glVertex3f(lowerx - correctionX, lowery - correctionY, 0.0);
				glTexCoord2i(0, 1); glVertex3f(lowerx - correctionX, lowery + sizey - correctionY, 0.0);
				glTexCoord2i(1, 1); glVertex3f(lowerx + sizex - correctionX, lowery + sizey - correctionY, 0.0);
				glTexCoord2i(1, 0); glVertex3f(lowerx + sizex - correctionX, lowery - correctionY, 0.0);
			glEnd();

			glDisable(GL_TEXTURE_2D);
		}
	}

	int wcsize = 4 * recSize;
	GLubyte weatherColors[wcsize];
	// draw weather data
	if (weatherAttrNum >= ATTR_MIN && weatherAttrNum < 4) {
		computeColors(weatherColors, wcsize, weatherData, recSize);

		// draw each row separately
		for (int currRow = 0; currRow < numRows - 1; currRow++) {
			glEnableClientState(GL_COLOR_ARRAY);
			glEnableClientState(GL_VERTEX_ARRAY);

			glColorPointer(4, GL_UNSIGNED_BYTE, 0, weatherColors);
			glVertexPointer(2, GL_FLOAT, 0, &weatherCoords[0]);
			glDrawElements(GL_TRIANGLE_STRIP, weatherIndices[currRow].size(),
					GL_UNSIGNED_INT, &weatherIndices[currRow][0]);

			glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_VERTEX_ARRAY);
		}
	}
	else if (weatherAttrNum >= 4 && weatherAttrNum <= ATTR_MAX) {
		computeDailyColors(weatherColors, wcsize, weatherData, recSize);

		// draw each row separately
		for (int currRow = 0; currRow < numRows - 1; currRow++) {
			glEnableClientState(GL_COLOR_ARRAY);
			glEnableClientState(GL_VERTEX_ARRAY);

			glColorPointer(4, GL_UNSIGNED_BYTE, 0, &weatherColors[0]);
			glVertexPointer(2, GL_FLOAT, 0, &weatherCoords[0]);
			glDrawElements(GL_TRIANGLE_STRIP, weatherIndices[currRow].size(),
					GL_UNSIGNED_INT, &weatherIndices[currRow][0]);

			glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_VERTEX_ARRAY);
		}
	}
	else {
		unreachable("redraw");
	}

	#ifdef DEBUG2
	// ****draw weather bounding box
	coord_t bottomLeft, bottomRight, topRight, topLeft;
	// doubled because we have x,y coords interleaved
	int pointsPerRow = numCols * 2;
	int pointsPerCol = numRows;

	float offset = 0.4;
	// get the four corners of the bounding box
	bottomLeft.x = weatherCoords[0] - offset;
	bottomLeft.y = weatherCoords[1] - offset;
	bottomRight.x = weatherCoords[pointsPerRow - 2] + offset;
	bottomRight.y = weatherCoords[pointsPerRow - 1] - offset;
	topRight.x = weatherCoords[pointsPerCol * pointsPerRow - 2] + offset;
	topRight.y = weatherCoords[pointsPerCol * pointsPerRow - 1] + offset;
	topLeft.x = weatherCoords[(pointsPerCol - 1) * pointsPerRow] - offset;
	topLeft.y = weatherCoords[(pointsPerCol - 1) * pointsPerRow + 1] + offset;

	float z = 0.0;
	glBegin(GL_LINE_LOOP);
		glLineWidth(1.0);
		glColor3ub(255, 0, 0);
		glVertex3f(bottomLeft.x, bottomLeft.y, z);
		glColor3ub(0, 255, 0);
		glVertex3f(bottomRight.x, bottomRight.y, z);
		glColor3ub(0, 0, 255);
		glVertex3f(topRight.x, topRight.y, z);
		glColor3ub(255, 255, 0);
		glVertex3f(topLeft.x, topLeft.y, z);
	glEnd();
	#endif

	// ****draw weather outline
	if (shouldDrawOutline) {
		glEnableClientState(GL_COLOR_ARRAY);
		glEnableClientState(GL_VERTEX_ARRAY);

		// draw the weather outline
		glColorPointer(3, GL_FLOAT, 6*sizeof(float), &weatherOutline[3]);
		glVertexPointer(3, GL_FLOAT, 6*sizeof(float), &weatherOutline[0]);
		glDrawArrays(GL_LINE_LOOP, 0, weatherOutline.size() / 6);
		
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}

	// TODO: is it possible to calculate the 8 from 24 hours/day
	// and samples every 3 hours?
	int samplesPerDay = HOURS_PER_DAY / 3;
	// compute the current day
	int day = (int)((currentTimeStep) / samplesPerDay);

	// ****draw modeled weather station data from csv files
	if (shouldDrawStations) {
		drawStations(day);
	}

	// ****draw shape data for all files
	if (shouldDrawShapes) {
		glLineWidth(1.0);
		for (int i = 0; i < shapeCoords.size(); i++) {
			drawShapedata(i);
		}
	}

	#ifdef DEBUG2
	// code to decipher conversion from screen to world coords
	printf("position = (%f, %f, %f)\n", eye[0], eye[1], eye[2]);
	double angle = 64.8385;
	cout << "tan(angle) = " << tan(angle) << endl;
	double offY = (double)eye[2] / tan(angle);
	if (offY < 0) offY *= -1;
	double offX = 1.0 * offY * ((double)screenWidth / (double)screenHeight);
	if (offX < 0) offX *= -1;

	// this will draw a rectangle just on the inside of the screen
	glBegin(GL_LINE_LOOP);
		//glColor3ub(255, 0, 0);
		glVertex3f(eye[0] - offX, eye[1] - offY, 0.0);
		glVertex3f(eye[0] - offX, eye[1] + offY, 0.0);
		glVertex3f(eye[0] + offX, eye[1] + offY, 0.0);
		glVertex3f(eye[0] + offX, eye[1] - offY, 0.0);
	glEnd();
	#endif

	// The line must be drawn HERE!
	glColor3f(0.54, 0.16, 0.88);
	glLineWidth(5.0);
	glBegin(GL_LINES);
		glVertex3f(lineStart.x, lineStart.y, 0.0);
		glVertex3f(lineEnd.x, lineEnd.y, 0.0);
	glEnd();
	#ifdef DEBUG2
	printf("Line is %f units long\n", calcDistance(lineStart.x, lineStart.y, lineEnd.x, lineEnd.y));
	printf("drawing line from (%.2f, %.2f) to (%.2f, %.2f)\n",
			lineStart.x, lineStart.y, lineEnd.x, lineEnd.y);
	#endif

	#ifdef DRAW_DEBUGX
	glLineWidth(3.0);
	glColor3ub(255, 0, 0);
	drawX(debugX, 0.25);
	#endif

	// indices of weather data corners at:
	// weatherCoords(0, 1)
	// weatherCoords(numCols * 2 - 2, numCols * 2 - 1)
	// weatherCoords((numRows - 1) * numCols * 2, (prev + 1))
	// weatherCoords(2 * numRows * numCols - 2, (prev + 1))

	// draw text data
	drawText(day);

	// draw the transfer legend and colorbar
	drawTransferLegend();
	
	// reset color and line size
	glColor3ub(255, 255, 255);
	glLineWidth(1.0);
	glutSwapBuffers();
	return;
}

void redraw2(void) {
	glutSetWindow(sliceWindow);
	glDisable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	int scsize = 4 * totalSliceSteps;
	GLubyte sliceColors[scsize];

	float sliceData[totalSliceSteps];
	float prevSliceData[totalSliceSteps];
	interpolateSliceGraph(sliceData, totalSliceSteps);

	int cosize = 2 * totalSliceSteps;
	float sliceCoords[cosize];

	// draw the slice data
	if (weatherAttrNum >= ATTR_MIN && weatherAttrNum < 4) {
		computeColors(sliceColors, scsize, sliceData, totalSliceSteps);
		computeSliceCoords(sliceCoords, cosize, sliceData, prevSliceData);

		glEnableClientState(GL_COLOR_ARRAY);
		glEnableClientState(GL_VERTEX_ARRAY);

		glColorPointer(4, GL_UNSIGNED_BYTE, 0, sliceColors);
		glVertexPointer(2, GL_FLOAT, 0, sliceCoords);
		glDrawArrays(GL_LINE_STRIP, 0, totalSliceSteps);

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}
	else if (weatherAttrNum >= 4 && weatherAttrNum <= ATTR_MAX) {
		if (currentTimeStep == 0) {
			for (int i = 0; i < totalSliceSteps; i++) {
				prevSliceData[i] = 0.0;
			}
		}
		else {
			currentTimeStep -= 1;
			interpolateSliceGraph(prevSliceData, totalSliceSteps);
			currentTimeStep += 1;
		}
		computeDailyColors(sliceColors, scsize, sliceData, totalSliceSteps);
		computeSliceCoords(sliceCoords, cosize, sliceData, prevSliceData);

		glEnableClientState(GL_COLOR_ARRAY);
		glEnableClientState(GL_VERTEX_ARRAY);
		
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, sliceColors);
		glVertexPointer(2, GL_FLOAT, 0, sliceCoords);
		glDrawArrays(GL_LINE_STRIP, 0, totalSliceSteps);

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}
	else {
		unreachable("redraw2");
	}

	// draw an outline around the data
	glColor3ub(255, 255, 255);
	glBegin(GL_LINE_LOOP);
		glVertex3f(0.0, 0.0, 0.0);
		glVertex3f(SLICE_GRAPH_WIDTH, 0.0, 0.0);
		glVertex3f(SLICE_GRAPH_WIDTH, SLICE_GRAPH_HEIGHT, 0.0);
		glVertex3f(0.0, SLICE_GRAPH_HEIGHT, 0.0);
	glEnd();
	
	for (int i = 0; i <= SLICE_GRAPH_HEIGHT; i += (SLICE_GRAPH_HEIGHT / 4.0)) {
		// draw tick marks on y axis
		glColor3ub(255, 255, 255);
		glBegin(GL_LINES);
			glVertex3f(0.0, i, 0.0);
			glVertex3f(-10.0, i, 0.0);
		glEnd();

		float xLoc = -50.0;
		float yOffset = 5.0;
		// draw values
		if (i == 0) {
			drawBitmapString(xLoc, i - yOffset, 0.0, LITTLE_FONT, "MIN");
		}
		else if (i == SLICE_GRAPH_HEIGHT) {
			drawBitmapString(xLoc, i - yOffset, 0.0, LITTLE_FONT, "MAX");
		}
		else {
			char val[11];
			float span = weatherAttrMax[weatherAttrNum] - weatherAttrMin[weatherAttrNum];
			snprintf(val, 10, "%d", (int)(span * (i / SLICE_GRAPH_HEIGHT)));
			glColor3ub(255, 255, 255);
			drawBitmapString(xLoc, i - yOffset, 0.0, LITTLE_FONT, val);
		}
	}

	for (int i = 0; i <= SLICE_GRAPH_WIDTH; i += (SLICE_GRAPH_WIDTH / 8)) {
		// draw tick marks on x axis
		glColor3ub(255, 255, 255);
		glBegin(GL_LINES);
			glVertex3f(i, 0.0, 0.0);
			glVertex3f(i, -10.0, 0.0);
		glEnd();
		
		// draw the 9 interpolated coords
		float step = SLICE_GRAPH_WIDTH / 8.0;
		char str[50];
		for (int i = 0; i < sliceLegendCoords.size(); i++) {
			snprintf(str, 49, "(%.1f,%.1f)", sliceLegendCoords[i].x, sliceLegendCoords[i].y);
			drawBitmapString(i * step - 55.0, -35.0, 0.0, LITTLE_FONT, str);
		}
	}
	
	glutSwapBuffers();
	glutPostRedisplay();
	return;
}

void drawBitmapString(float x, float y, float z, void *font, char *string) {
    char *c;
    glRasterPos3f(x, y, z);
    for (c = string; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
	return;
}

void drawTriangle(coord_t center, float size) {
    glBegin(GL_LINE_LOOP);
		glVertex3f(center.x, center.y + size, center.z);
		glVertex3f(center.x - size, center.y - size, center.z);
		glVertex3f(center.x + size, center.y - size, center.z);
    glEnd();
    return;
}

void drawX(coord_t center, float size) {
	glBegin(GL_LINES);
		glVertex3f(center.x - size, center.y + size, center.z);
		glVertex3f(center.x + size, center.y - size, center.z);
		glVertex3f(center.x + size, center.y + size, center.z);
		glVertex3f(center.x - size, center.y - size, center.z);
	glEnd();
	return;
}

void drawStations(int day) {
	#ifdef DEBUG2
	printf("currentTimeStep = %d day = %d\n", currentTimeStep, day);
	#endif

	// stay in the limits
	if (day > 365) {
		//cerr << "Error: Day was out of bounds" << endl;
		// may need to change this depending on desired behavior
		currentTimeStep = 0;
		return;
	}

	// size of the station data triangles
	float stationSize = 0.025;

	for (int stationNum = 0; stationNum < csvCoords.size(); stationNum++) {
		// normalize the data
		float val = csvData[day][stationNum];
		int tSize = transFuncData.size();
		GLubyte newRed, newGreen, newBlue;

		// check if the value is outside the bounds of the transfer function
		if (val <= transFuncData[0].value) {
			newRed = transFuncData[0].R;
			newGreen = transFuncData[0].G;
			newBlue = transFuncData[0].B;
		}
		else if (val >= transFuncData[tSize - 1].value) {
			newRed = transFuncData[tSize - 1].R;
			newGreen = transFuncData[tSize - 1].G;
			newBlue = transFuncData[tSize - 1].B;
		}
		else {
			// calculate the color using transfer function
			int colorIndex;
			// find which interval this value lies in
			for(colorIndex = 0; colorIndex < tSize; colorIndex++) {
				if (transFuncData[colorIndex].value > val) break;
			}

			// calculate lower color data
			float lowVal = transFuncData[colorIndex - 1].value;
			GLubyte red1 = transFuncData[colorIndex - 1].R;
			GLubyte green1 = transFuncData[colorIndex - 1].G;
			GLubyte blue1 = transFuncData[colorIndex - 1].B;

			// calculate upper color data
			float highVal = transFuncData[colorIndex].value;
			GLubyte red2 = transFuncData[colorIndex].R;
			GLubyte green2 = transFuncData[colorIndex].G;
			GLubyte blue2 = transFuncData[colorIndex].B;

			float diff = highVal - lowVal;

			// linearly interpolate the new color
			newRed = (1.0 - ((val - lowVal)/diff))*red1 + (1.0 - ((highVal - val)/diff))*red2;
			newGreen = (1.0 - ((val - lowVal)/diff))*green1 + (1.0 - ((highVal - val)/diff))*green2;
			newBlue = (1.0 - ((val - lowVal)/diff))*blue1 + (1.0 - ((highVal - val)/diff))*blue2;
		}
		
		coord_t station = csvCoords[stationNum];
		// draw the stations as triangles
		glColor4ub(newRed, newGreen, newBlue, transparency);
		glBegin(GL_TRIANGLES);
			glVertex3f(station.x, station.y + stationSize, station.z);
			glVertex3f(station.x - stationSize, station.y - stationSize, station.z);
			glVertex3f(station.x + stationSize, station.y - stationSize, station.z);
		glEnd();

		// outline the stations for visability
		glColor3ub(0, 0, 0);
		drawTriangle(station, stationSize);
	}
	return;
}

void drawText(int totalDays) {
	string year = "";
	char *month;
	char day[3];

	// index this at 1, not 0
	totalDays++;

	if (totalDays < 62) year += ", 2001";
	else year += ", 2002";

	// find the date based on 11-1
	if (totalDays < 31) {
		month = "November ";
	}
	else if (totalDays < 62) {
		month = "Decemeber ";
		totalDays -= 30;
	}
	else if (totalDays < 93) {
		month = "January ";
		totalDays -= 61;
	}
	else if (totalDays < 121) {
		month = "February ";
		totalDays -= 92;
	}
	else if (totalDays < 152) {
		month = "March ";
		totalDays -= 120;
	}
	else if (totalDays < 182) {
		month = "April ";
		totalDays -= 151;
	}
	else if (totalDays < 213) {
		month = "May ";
		totalDays -= 181;
	}
	else if (totalDays < 243) {
		month = "June ";
		totalDays -= 212;
	}
	else if (totalDays < 274) {
		month = "July ";
		totalDays -= 242;
	}
	else if (totalDays < 305) {
		month = "August ";
		totalDays -= 273;
	}
	else if (totalDays < 335) {
		month = "September ";
		totalDays -= 304;
	}
	else if (totalDays < 365) {
		month = "October ";
		totalDays -= 333;
	}
	else {
		// reset the time
		currentTimeStep = 0;
	}

	// UpperLeft and LowerRight coords for the text background
	coord_t upperLeft, lowerRight;
	if (datePosition == TEXT_LOWER_LEFT) {
		setCoord(upperLeft, 10, screenHeight - 60, 0.0);
		setCoord(lowerRight, upperLeft.x + 187, screenHeight - 10, 0.0);
	}
	else if (datePosition == TEXT_UP) {
		setCoord(upperLeft, 0.5 * screenWidth, 10, 0.0);
		setCoord(lowerRight, upperLeft.x + 187, upperLeft.y + 50, 0.0);
	}
	else if (datePosition == TEXT_DOWN) {
		setCoord(upperLeft, 0.5 * screenWidth, screenHeight - 60, 0.0);
		setCoord(lowerRight, upperLeft.x + 187, screenHeight - 10, 0.0);
	}

	coord_t first = screen2worldCoords(upperLeft.x, upperLeft.y, eye[2] - TEXT_DIST);
	coord_t second = screen2worldCoords(lowerRight.x, lowerRight.y, eye[2] - TEXT_DIST);

	// draw a black background behind the date and attribute type
	glColor3ub(0, 0, 0);
	glBegin(GL_QUADS);
		glVertex3f(first.x, first.y, first.z);
		glVertex3f(first.x, second.y, first.z);
		glVertex3f(second.x, second.y, first.z);
		glVertex3f(second.x, first.y, first.z);
	glEnd();

	// accumulate the date into one string
	string date;
	snprintf(day, 3, "%d", totalDays);
	date += month;
	date += day;
	date += year;
	
	// calculate date text position
	float dateOffsetX = (screenWidth - (2 * upperLeft.x) - 30) / (float)screenWidth;
	float dateOffsetY = ((2 * upperLeft.y + 75) / (float)screenHeight) - 1;
	// keep this, the above was derived from it
	//float dateOffsetY = (screenHeight - (2 * (screenHeight-upperLeft.y)) + 80) / (float)screenHeight;
	float attrOffsetX = (screenWidth - (2 * upperLeft.x) - 30) / (float)screenWidth;
	float attrOffsetY = ((2 * upperLeft.y + 35) / (float)screenHeight) - 1;

	// set the current attribute name
	char *attr;
	switch (weatherAttrNum) {
		case SNOWPACK:
			attr = "    Snowpack";
			break;
		case SNOWFALL:
			attr = "    Snowfall";
			break;
		case PRECIPITATION:
			attr = "    Precipitation";
			break;
		case RUNOFF:
			attr = "    Runoff";
			break;
		case SNOWPACK_DAILY:
			attr = "Daily Snowpack";
			break;
		case SNOWFALL_DAILY:
			attr = "Daily Snowfall";
			break;
		case PRECIPITATION_DAILY:
			attr = "Daily Precipitation";
			break;
		case RUNOFF_DAILY:
			attr = "Daily Runoff";
			break;
		default:
			unreachable("drawText()");
			attr = "ERROR!";
	}

	// set up the text view
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glColor3ub(255, 255, 255);

	// draw the current date
	drawBitmapString(eye[0] - dateOffsetX, eye[1] - dateOffsetY, eye[2], BIG_FONT, (char*)date.c_str());
	// draw the current weather attribute name
	drawBitmapString(eye[0] - attrOffsetX, eye[1] - attrOffsetY, eye[2], BIG_FONT, attr);

	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
			
	return;
}

void drawTransferLegend(void) {
	int uLeftX = 10;
	int uLeftY = 10;
	int lRightX = 110;
	int lRightY = 0.75 * screenHeight;
	coord_t uLeft, lRight, proxy;
	
	// get the world coords for the background of the colorbar
	uLeft = screen2worldCoords(uLeftX, uLeftY, eye[2] - TEXT_DIST);
	lRight = screen2worldCoords(lRightX, lRightY, eye[2] - TEXT_DIST);
	// draw the background
	glColor3ub(0, 0, 0);
	glBegin(GL_QUADS);
		glVertex3f(uLeft.x, uLeft.y, uLeft.z);
		glVertex3f(uLeft.x, lRight.y, uLeft.z);
		glVertex3f(lRight.x, lRight.y, uLeft.z);
		glVertex3f(lRight.x, uLeft.y, uLeft.z);
	glEnd();

	vector<trans_t> colors;
	vector<coord_t> coords;

	const int SPACING = 7;
	// get the world coords for the actual colorbar
	uLeft = screen2worldCoords(uLeftX + ((lRightX - uLeftX) / 2.0), uLeftY + SPACING, eye[2] - TEXT_DIST);
	lRight = screen2worldCoords(lRightX - SPACING, lRightY - SPACING, eye[2] - TEXT_DIST);

	// normal weather attributes
	if (0 <= weatherAttrNum && weatherAttrNum <= 3) {
		// compute the colors based on the transfer file
		long fullSize = transFuncData.size();
		for (int i = 0; i < fullSize; i++) {
			// we want duplicates of the ends
			if ((i == 0) || (i == fullSize - 1)) {
				colors.push_back(transFuncData[i]);
				colors.push_back(transFuncData[i]);
			}
			// push twice because we have a 2D bar, not just a line
			colors.push_back(transFuncData[i]);
			colors.push_back(transFuncData[i]);
		}

		fullSize = colors.size();
		long halfSize = fullSize / 2;
		// compute the coord locatations
		for (int i = 0; i < halfSize; i++) {
			if (i == 0) {
				// left side
				proxy.x = uLeft.x;
				proxy.y = lRight.y;
				proxy.z = uLeft.z;
				coords.push_back(proxy);
				// right side
				proxy.x = lRight.x;
				coords.push_back(proxy);
				continue;
			}
			// normalize the value
			double scaled = lRight.y - ((double)i / (double)(halfSize-1)) * (lRight.y - uLeft.y);
			// left side
			proxy.x = uLeft.x;
			proxy.y = scaled;
			#ifdef DEBUG2
			printf("i = %d loc = (%d, %d)\n", i, (int)uLeft.x, (int)scaled);
			printf("i = %d loc = (%d, %d)\n", i, (int)lRight.x, (int)scaled);
			#endif
			coords.push_back(proxy);
			// right side
			proxy.x = lRight.x;
			coords.push_back(proxy);
		}

		// draw the colorbar and text
		// TODO: call this once or multiple times depending on transfer file
		drawColorbar(colors, coords, (attribute_t)weatherAttrNum);
	}
	// daily weather attributes
	else if (weatherAttrNum == SNOWPACK_DAILY) {
		trans_t val;
		// temporary, will be changed to take input from file
		setTrans(val, 255, 0, 0, weatherAttrMin[weatherAttrNum]);
		colors.push_back(val);
		colors.push_back(val);
		setTrans(val, 255, 255, 255, 0.0);
		colors.push_back(val);
		colors.push_back(val);
		setTrans(val, 0, 0, 255, weatherAttrMax[weatherAttrNum]);
		colors.push_back(val);
		colors.push_back(val);

		coord_t c;
		int halfSize = colors.size() / 2;
		for (int i = 0; i < halfSize; i++) {
			float offset = ((float)i / (halfSize - 1)) * (lRight.y - uLeft.y);
			setCoord(c, uLeft.x, lRight.y - offset, uLeft.z);
			coords.push_back(c);
			setCoord(c, lRight.x, lRight.y - offset, uLeft.z);
			coords.push_back(c);
		}

		// draw the colorbar and text
		drawColorbar(colors, coords, (attribute_t)weatherAttrNum);
	}
	else if (weatherAttrNum == SNOWFALL_DAILY) {
	}
	else if (weatherAttrNum == PRECIPITATION_DAILY) {
		trans_t val;
		// temporary, will be changed to take input from file
		setTrans(val, 85, 0, 85, weatherAttrMin[weatherAttrNum]);
		colors.push_back(val);
		colors.push_back(val);
		setTrans(val, 255, 255, 255, weatherAttrMax[weatherAttrNum]);
		colors.push_back(val);
		colors.push_back(val);

		coord_t c;
		int halfSize = colors.size() / 2;
		for (int i = 0; i < halfSize; i++) {
			float offset = ((float)i / (halfSize - 1)) * (lRight.y - uLeft.y);
			setCoord(c, uLeft.x, lRight.y - offset, uLeft.z);
			coords.push_back(c);
			setCoord(c, lRight.x, lRight.y - offset, uLeft.z);
			coords.push_back(c);
		}

		// draw the colorbar and text
		drawColorbar(colors, coords, (attribute_t)weatherAttrNum);
	}
	else if (weatherAttrNum == RUNOFF_DAILY) {
	}
	else {
		unreachable("drawTransferLegend");
	}
	return;
}

void drawColorbar(vector<trans_t> colors, vector<coord_t> coords, attribute_t type) {
	// make sure we have same number of colors as coords
	if (colors.size() != coords.size()) return;

	char val[10];
	float offset;
	// boundaries for the weather attribute values in %, not dependent on screen size
	float startWordX = (screenWidth - 35) / (float)screenWidth;
	float startWordY = (screenHeight - 50) / (float)screenHeight;
	float endWordY = 0.475;

	// draw the colorbar
	glBegin(GL_QUAD_STRIP);
	for (int i = 0; i < colors.size(); i++) {
		glColor3ub(colors[i].R, colors[i].G, colors[i].B);
		glVertex3f(coords[i].x, coords[i].y, coords[i].z);
	}
	glEnd();
	
	// draw the colorbar values
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glColor3ub(255, 255, 255);
	glLoadIdentity();
	int halfSize = colors.size() / 2;
	// draw the weather attribute values
	for (int i = 0; i < halfSize; i++) {
		if (i == 0) {
			if (weatherAttrNum != SNOWPACK) snprintf(val, 9, "%dmm", 0);
			else snprintf(val, 9, "%dmm", (int)weatherAttrMin[weatherAttrNum]);
		}
		else if (i == halfSize - 1) snprintf(val, 9, "%dmm", (int)weatherAttrMax[weatherAttrNum]);
		else snprintf(val, 9, "%dmm", (int)colors[i * 2].value);

		offset = startWordY - ((halfSize - i - 1) * (startWordY+endWordY) / (halfSize-1));

		// draw the text
		drawBitmapString(eye[0] - startWordX, eye[1] + offset, eye[2], LITTLE_FONT, val);
	}
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	return;
}

void drawShapedata(int fileNum) {
	int startIndex, numPoints;

	// white is easy to see
	glColor3ub(255, 255, 255);

	#ifdef SHP_TIMING_ON
	double elapsedTime;
	clock_t startTime, endTime;
	// start timing
	startTime = clock();
	#endif

	// draw all entities
	for (int currEntity = 0; currEntity < shapeCoords[fileNum].size(); currEntity++) {
		for (int currPart = 0; currPart < partOffsets[fileNum][currEntity].size() - 1; currPart++) {
			// get the start and end index for this part
			startIndex = partOffsets[fileNum][currEntity][currPart];
			numPoints = partOffsets[fileNum][currEntity][currPart + 1] - startIndex;

			glEnableClientState(GL_VERTEX_ARRAY);

			// draw the shape data efficiently
			glVertexPointer(2, GL_FLOAT, 0, &shapeCoords[fileNum][currEntity][0]);
			glDrawArrays(GL_LINE_LOOP, startIndex, numPoints);

			glDisableClientState(GL_VERTEX_ARRAY);
		}
	}

	#ifdef SHP_TIMING_ON
	// stop timing
	endTime = clock();
	// calculate the elapsed time
	elapsedTime = (double)(endTime - startTime);
	printf("Map number %d drawn in %f seconds.\n", fileNum, (double)elapsedTime / (double)CLOCKS_PER_SEC);
	#endif
	return;
}

// this should be fixed, but it hasn't been tested by rotating camera about x axis
coord_t screen2worldCoords(int screenX, int screenY, float worldZ) {
	#ifdef DEBUG2
	printf("screen2world(%d, %d) at z = %f\n", screenX, screenY, worldZ);
	#endif
	coord_t world;

	// get the distance from the center of the screen to the bottom of the screen
	double offY = ((double)eye[2] - worldZ) / tanViewAngle;
	if (offY < 0.0) offY *= -1;
	// calculate the distance from center to side
	double offX = offY * ((double)screenWidth / (double)screenHeight);
	if (offX < 0.0) offX *= -1;

	double halfWidth = screenWidth / 2.0;
	double halfHeight = screenHeight / 2.0;

	// normalize to a value between -1 and 1
	double dx = (screenX - halfWidth) / halfWidth;
	double dy = (screenY - halfHeight) / halfHeight;
	// scale the value by the distance in world coords
	dx *= offX;
	dy *= offY;

	world.x = eye[0] + dx;
	world.y = eye[1] - dy;
	world.z = worldZ;

	#ifdef DEBUG2
	printf("world coord = (%f, %f, %f)\n", world.x, world.y, world.z);
	#endif
	return world;
}

coord_t points2vector(coord_t a, coord_t b) {
	coord_t result = {b.x - a.x, b.y - a.y};
	return result;
}

double calcDistance(float x1, float y1, float x2, float y2) {
	return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

// Note: matrices must be in column major order
void solve4x4Lapack(double *a, double *b) {
	int n = 4, lda = 4, ldb = 4, nrhs = 1, info, ipiv[4];

	dgesv_(&n, &nrhs, a, &lda, ipiv, b, &ldb, &info);

	return;
}

void calcSliceSteps(void) {
	// get the length of the line in pixels
	double pixelLength = calcDistance(screenStart.x, screenStart.y, screenEnd.x, screenEnd.y);

   	if (pixelLength <= 1000.0) totalSliceSteps = (int)pixelLength;
	else totalSliceSteps = 1000;

	return;
}

void interpolateSliceGraph(float *sliceData, int sdsize) {
	float t, x, y;
	double vala, valb, valc, vald;
	double xa, ya, xb, yb, xc, yc, xd, yd;
	// TODO: take these lines out, not consistent:(i < 2*recSize - pointsPerRow)
	int pointsPerRow = numCols * 2;
	int pointsPerCol = numRows;
	bool insideGraph = false;

	// switch the points if they're out of order by x coord
	// we want a left-to-right graph
	if (lineStart.x > lineEnd.x) {
		coord_t c = lineStart;
		lineStart = lineEnd;
		lineEnd = c;
	}

	/* allocate space for the slice data
	if (snowpackSliceData == NULL)
		snowpackSliceData = new float[totalTimeSteps * totalSliceSteps];
	if (snowfallSliceData == NULL)
		snowfallSliceData = new float[totalTimeSteps * totalSliceSteps];
	if (precipitationSliceData == NULL)
		precipitationSliceData = new float[totalTimeSteps * totalSliceSteps];
	if (runoffSliceData == NULL)
		runoffSliceData = new float[totalTimeSteps * totalSliceSteps];
	// */

	// ****calculate the actual data for the slice graph

	int index = 0;
	// bilinearly interpolate the data for all attributes
	for (int sliceStep = 0; sliceStep < totalSliceSteps; sliceStep++) {
		t = (float)sliceStep / totalSliceSteps;
		x = (1-t)*lineStart.x + t*lineEnd.x;
		y = (1-t)*lineStart.y + t*lineEnd.y;

		// if we haven't found a point inside the grid
		if (!insideGraph) {
			// see if this point is inside the grid
			if (findFirstCell(x, y, index)) {
				insideGraph = true;
			}
		}

		// get the next interpolation index
		if (!nextInterpolationPoint(x, y, index, 1)) {
			insideGraph = false;
		}

		// make sure the index is in range
		if ((index > 2*recSize - pointsPerRow) || (index % pointsPerRow == (pointsPerRow - 1))) {
			fprintf(stderr, "Index %d out of range at sliceStep = %d\n", index, sliceStep);
			unreachable("interpolateSliceGraph");
			return;
		}

		// if this step on the line is out of the grid
		if (!insideGraph) {
			// store null values in the arrays
			sliceData[sliceStep] = 0.0;
			continue;
		}

		// get the coords of the cell
		xa = (double)weatherCoords[index];
		ya = (double)weatherCoords[index + 1];
		xb = (double)weatherCoords[index + 2];
		yb = (double)weatherCoords[index + 3];
		xc = (double)weatherCoords[index + pointsPerRow + 2];
		yc = (double)weatherCoords[index + pointsPerRow + 3];
		xd = (double)weatherCoords[index + pointsPerRow];
		yd = (double)weatherCoords[index + pointsPerRow + 1];

		// get the data for the 4 corners of the cell for all attributes
		vala = (double)weatherData[currentTimeStep*recSize + (index/2)];
		valb = (double)weatherData[currentTimeStep*recSize + (index/2 + 1)];
		valc = (double)weatherData[currentTimeStep*recSize + (index/2 + numCols + 1)];
		vald = (double)weatherData[currentTimeStep*recSize + (index/2 + numCols)];

		// set up the coeffecient matrix in column major order
		double A[16] = {
			1.0, 1.0, 1.0, 1.0,
			xa, xb, xc, xd,
			ya, yb, yc, yd,
			xa*ya, xb*yb, xc*yc, xd*yd
		};

		// set up the answer column for all attributes
		double B[4] = {vala, valb, valc, vald};

		// solve the 4 simultaneous equations
		solve4x4Lapack(A, B);

		// compute the final value for the current interpolation point
		float sliceValue = B[0] + B[1]*x + B[2]*y + B[3]*x*y;

		#ifdef DEBUG2
		if (sliceValue < 0.0) {
			fprintf(stderr, "ERROR[%d]: Snowpack cannot be negative. val = %f at (%f,%f)\n",
					numErrors++, sliceValue, x, y);
		}
		if (precSliceValue < 0.0) {
			fprintf(stderr, "ERROR[%d]: Precipitation cannot be negative. val = %f at (%f,%f)\n",
					numErrors++, precSliceValue, x, y);
		}
		#endif

		// store the value in the slice data array
		sliceData[sliceStep] = sliceValue;
	}
	#ifdef CONSOLE_OUTPUT2
	cout << "Done interpolating. timeStep = " << currentTimeStep << endl;
	#endif

	// ****calculate the coords for the x axis

	sliceLegendCoords.push_back(lineStart);
	sliceLegendCoords.push_back(lineEnd);

	float diffx = sliceLegendCoords[1].x - sliceLegendCoords[0].x;
	float diffy = sliceLegendCoords[1].y - sliceLegendCoords[0].y;
	// divide each interval by SLICE_XAXIS_COORDS to get the step
	float stepx = diffx / SLICE_XAXIS_COORDS;
	float stepy = diffy / SLICE_XAXIS_COORDS;
	// get the starting point
	coord_t start = sliceLegendCoords[0];
	// save the last point and pop it
	coord_t end = sliceLegendCoords[1];
	sliceLegendCoords.pop_back();

	coord_t next;
	// compute the rest of the coords for the x-axis of the slice graph
	for (int i = 1; i < SLICE_XAXIS_COORDS; i++) {
		next.x = sliceLegendCoords[0].x + i * stepx;
		next.y = sliceLegendCoords[0].y + i * stepy;
		sliceLegendCoords.push_back(next);
	}
	// restore the last point
	sliceLegendCoords.push_back(end);
	
	return;
}

// checks to see if (x,y) is inside the cell with bottom-left point index i
// TODO: make sure i by ref is OK
bool insideCell(float x, float y, int &i) {
	int pointsPerRow = numCols * 2;
	coord_t bottomLeft, bottomRight, topRight, topLeft;

	// get the coords of the 4 corners of this cell
	bottomLeft.x = weatherCoords[i];
	bottomLeft.y = weatherCoords[i + 1];
	bottomRight.x = weatherCoords[i + 2];
	bottomRight.y = weatherCoords[i + 3];
	topLeft.x = weatherCoords[i + pointsPerRow];
	topLeft.y = weatherCoords[i + pointsPerRow + 1];
	topRight.x = weatherCoords[i + pointsPerRow + 2];
	topRight.y = weatherCoords[i + pointsPerRow + 3];

	coord_t c = {x, y, 0.0};
	// return false if it is on the outside any edge
	if (aboveOrBelowLine(bottomLeft, bottomRight, c) == BELOW_LINE) return false;
	if (aboveOrBelowLine(bottomRight, topRight, c) == ABOVE_LINE) return false;
	if (aboveOrBelowLine(topLeft, topRight, c) == ABOVE_LINE) return false;
	if (aboveOrBelowLine(topLeft, bottomLeft, c) == BELOW_LINE) return false;

	return true;
}

// index is set the the index of the x coord of the lower left corner
bool findFirstCell(float x, float y, int &index) {
	#ifdef DEBUG2
	printf("findFirstCell(%f,%f)\n", x, y);
	#endif

	coord_t c = {x, y, 0.0};
	coord_t bottomLeft, bottomRight, topRight, topLeft;
	int pointsPerRow = numCols * 2;
	int pointsPerCol = numRows;
	int i;
	// check each cell of weatherData to see if (x,y) is in it
	for (int row = 0; row < numRows - 1; row++) {
		for (int col = 0; col < 2*(numCols - 1); col += 2) {
			i = row * pointsPerRow + col;

			bottomLeft.x = weatherCoords[i];
			bottomLeft.y = weatherCoords[i + 1];
			// first check if the point is far from the bottom corner
			if (calcDistance(x, y, bottomLeft.x, bottomLeft.y) > 1.0) continue;

			bottomRight.x = weatherCoords[i + 2];
			bottomRight.y = weatherCoords[i + 3];
			// check if the point is below the bottom edge
			if (aboveOrBelowLine(bottomLeft, bottomRight, c) == BELOW_LINE) continue;

			topLeft.x = weatherCoords[i + pointsPerRow];
			topLeft.y = weatherCoords[i + pointsPerRow + 1];
			// check if the point is below(outside) the left edge
			if (aboveOrBelowLine(topLeft, bottomLeft, c) == BELOW_LINE) continue;

			topRight.x = weatherCoords[i + pointsPerRow + 2];
			topRight.y = weatherCoords[i + pointsPerRow + 3];
			// check if the point is above(outside) the right edge
			if (aboveOrBelowLine(bottomRight, topRight, c) == ABOVE_LINE) continue;

			// check if the point is above the top edge
			if (aboveOrBelowLine(topRight, topLeft, c) == ABOVE_LINE) continue;

			// the point is in the cell
			index = i;
			return true;
		}
	}
	return false;
}

// Recursively checks if the point (x,y) is inside the cell with bottom left
// corner == index. If the point is outside an edge of a cell, the insideCell()
// function will set index to the bottom left corner of the next adjacent cell in
// the direction of the point.
bool nextInterpolationPoint(float x, float y, int &index, int level) {
	int pointsPerRow = 2 * numCols;
	// if recursed a bunch already
	if (level > 10) {
		// just check the whole grid
		if (findFirstCell(x, y, index)) return true;
		else return false;
	}

	// make sure the index to check is in range
	if ((index > 2*recSize - pointsPerRow) || (index % pointsPerRow == (pointsPerRow - 1))) {
		return false;
	}

	// return true if it's inside this cell
	if (insideCell(x, y, index)) return true;
	// keep checking if it's not
	else return nextInterpolationPoint(x, y, index, level + 1);

	unreachable("nextInterpolationPoint");
	return true;
}

void getNcFileData(char **fileList) {
	for (int fileNum = 0; fileNum < numNcFiles; fileNum++) {
		char *fileName = fileList[fileNum];

		NcFile ncF(fileName);
		// make sure the file is valid
		if (!ncF.is_valid()) {
			#ifndef ERROR_NOTIFICATION_OFF
			printf("Error: %s is not a valid Ncfile.", fileName);
			#endif
			return;
		}

		#ifdef CONSOLE_OUTPUT
		int startPos = 0;
		// strip the prefix off the filename; it's ugly
		for (int i = 0; i < strlen(fileName); i++) {
			if (fileName[i] == '/') startPos = i + 1;
		}
		// check for index error
		if (startPos > strlen(fileName) - 1) startPos = 0;
		printf("Processing Ncfile[%d]: %s\n", fileNum, fileName + startPos);
		#endif

		// select which variables to access
		NcVar *snowVar = ncF.get_var("SNOW");
		NcVar *snowncVar = ncF.get_var("SNOWNC");
		NcVar *raincVar = ncF.get_var("RAINC");
		NcVar *rainncVar = ncF.get_var("RAINNC");
		NcVar *sfroffVar = ncF.get_var("SFROFF");
		NcVar *udroffVar = ncF.get_var("UDROFF");

		// get references to values
		NcValues *snowVals = snowVar->values();
		NcValues *snowncVals = snowncVar->values();
		NcValues *raincVals = raincVar->values();
		NcValues *rainncVals = rainncVar->values();
		NcValues *sfroffVals = sfroffVar->values();
		NcValues *udroffVals = udroffVar->values();

		long fileOffset = fileNum * timeSize * recSize;
		// go through each timestep
		for (int timeStep = 0; timeStep < timeSize; timeStep++) {
			long timeOffset = timeStep * recSize;
			// go through each coordinate
			for (int i = 0; i < recSize; i++) {
				long varOffset, recOffset = timeOffset + i;
				long totalOffset = fileOffset + recOffset;

				// hack to make corrupt data file work
				if (fileNum == 29 && timeStep > 3) varOffset = 3 * recSize + i;
				else varOffset = recOffset;

				// accumulate the data in reverse order
				snowpackData[totalOffset] = snowVals->as_float(varOffset);
				snowfallData[totalOffset] = snowncVals->as_float(varOffset);
				precipitationData[totalOffset] = raincVals->as_float(varOffset) + rainncVals->as_float(varOffset);
				runoffData[totalOffset] = sfroffVals->as_float(varOffset) + udroffVals->as_float(varOffset);
			}
		}
	}
	return;
}

// get the data for one shape file
int getShapeFileData(int fileNum, char *fileName) {
	int nEntities, shapeType, totalParts = 0, totalPoints = 0;
	double minCoords[4], maxCoords[4];

	// initialize vectors for this file
	shapeCoords.push_back(floatVectorVector);
	partOffsets.push_back(intVectorVector);

	// get a handle for the shapefile
	SHPHandle hSHP = SHPOpen(fileName, "rb");
	// return error if file is not valid
	if (hSHP == NULL) {
		//fprintf(stderr, "Error: %s is not a shapefile. Skipping\n", fileName);
		return -1;
	}
	SHPGetInfo(hSHP, &nEntities, &shapeType, minCoords, maxCoords);
	
	// good debug info
	#ifdef CONSOLE_OUTPUT
	printf("Processing shapefile[%d] %s\n", fileNum, fileName);
	#endif

	// go though all the entities
	for (int currEntity = 0; currEntity < nEntities; currEntity++) {
		// initialize vectors
		partOffsets[fileNum].push_back(intVector);
		shapeCoords[fileNum].push_back(floatVector);

		// get a reference to the current object
		SHPObject *sObj = SHPReadObject(hSHP, currEntity);
		
		#ifdef DEBUG2
		printf("type = %d id = %d, parts = %d nVertices = %d\n",
			   	sObj->nSHPType, sObj->nShapeId, sObj->nParts, sObj->nVerticed);
		#endif

		// get the offsets for each part separately
		for (int currPart = 0; currPart < sObj->nParts; currPart++) {
			partOffsets[fileNum][currEntity].push_back(sObj->panPartStart[currPart]);
			#ifdef DEBUG2
			printf("Processing entity %d, part %d ", currEntity, currPart);
			printf("panPartStart[%d] = %d\n", currPart, sObj->panPartStart[currPart]);
			#endif
		}
		// make sure the get the last index so we know where to stop for the final part
		partOffsets[fileNum][currEntity].push_back(sObj->nVertices);

		// get the vertex data for the entire entity
		for (int currVertex = 0; currVertex < sObj->nVertices; currVertex++) {
			// interleave (x,y) coords; this actually makes it easier to render
			shapeCoords[fileNum][currEntity].push_back(sObj->padfX[currVertex]);
			shapeCoords[fileNum][currEntity].push_back(sObj->padfY[currVertex]);

			totalPoints++;

			#ifdef DEBUG2
			printf("\tx[%d] = %f, y[%d] = %f\n",
				   	currVertex, sObj->padfX[currVertex], currVertex, sObj->padfY[currVertex]);
			#endif
		}
		totalParts += sObj->nParts;
		
		SHPDestroyObject(sObj);
	}
	
	#ifdef DEBUG2
	printf("File %d has %d entities, %d total parts and %d total points\n",
		   	fileNum, nEntities, totalParts, totalPoints);
	#endif
	
	SHPClose(hSHP);
	return 0;
}

void jpeg2texture(int texNum, char *imageName) {
	GLuint success = 0;
	
	success = ilutGLLoadImage(imageName);
	// check for error
	if (ilGetError()) {
		printf("Error was: %s\n", iluErrorString(ilGetError()));
		return;
	}

	// if the image was loaded successfully
	if (success) {
		success = ilConvertImage(IL_RGB, IL_UNSIGNED_BYTE);
	   	if (!success) {
			#ifndef ERROR_NOTIFICATION_OFF
			fprintf(stderr, "Error converting image \"%s\".\n", imageName);
			#endif
		   	return;
		}
		// bind this texture to a number
		glBindTexture(GL_TEXTURE_2D, textures[texNum]);

		// TODO: see if these are the parameters we really want
	   	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	   	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

		// declare texture attributes
	   	glTexImage2D(GL_TEXTURE_2D, 0, ilGetInteger(IL_IMAGE_BPP), ilGetInteger(IL_IMAGE_WIDTH),
			   	ilGetInteger(IL_IMAGE_HEIGHT), 0, ilGetInteger(IL_IMAGE_FORMAT), GL_UNSIGNED_BYTE, ilGetData());
   	} 
	return;
}

void parseImageLocation(char *fileName) {
	int i = 0;
	float num;
	coord_t texCoord;

	// get the longitude
	while((fileName[i] != 'W') && (fileName[i] != 'E')) i++;
	num = atof(fileName + i + 1);

	// west longitudes are actually negative
	if (fileName[i] == 'W') texCoord.x = -num;
	else if (fileName[i] == 'E') texCoord.x = num;
	else {
		// error
		#ifndef ERROR_NOTIFICATION_OFF
		cerr << "Error: Malformed filename. Should be: path/[E|W]<number>[N|S]<number>.jpg" << endl;
		#endif
		return;
	}

	// get the latitude
	while((fileName[i] != 'S') && (fileName[i] != 'N')) i++;
	num = atof(fileName + i + 1);
	// south latitudes are actually negative
	if (fileName[i] == 'S') texCoord.y = -num;
	else if (fileName[i] == 'N') texCoord.y = num;
	else {
		// error
		#ifndef ERROR_NOTIFICATION_OFF
		cerr << "Error: Malformed filename. Should be: path/[E|W]<number>[N|S]<number>.jpg" << endl;
		#endif
		return;
	}

	#ifdef CONSOLE_OUTPUT
	printf("    %s x = %.2f y = %.2f\n", fileName, texCoord.x, texCoord.y);
	#endif

	texCoords.push_back(texCoord);
	return;
}

void computeColors(GLubyte *weatherColors, int wcsize, float *data, int dataSize) {
	if (wcsize != 4 * recSize && wcsize != 4 * totalSliceSteps) {
		unreachable("computeColors");
		return;
	}

	for (int dataStep = 0; dataStep < wcsize; dataStep += 4) {
		long totalOffset = (currentTimeStep * dataSize) + (dataStep / 4) - 1;
		#ifdef DEBUG2
		printf("timestep = %d totalOffset = %ld\n", currentTimeStep, totalOffset);
		#endif

		#ifndef SLICE_COLOR_ON
		// only do this for the slice graph
		if (wcsize == 4 * totalSliceSteps) {
			// make the line white
			weatherColors[dataStep + 0] = 255;
			weatherColors[dataStep + 1] = 255;
			weatherColors[dataStep + 2] = 255;
			weatherColors[dataStep + 3] = 0;
			continue;
		}
		#endif


		long tfsize = transFuncData.size();

		float val = data[totalOffset];

		// check if the value is outside the bounds of the transfer function
		if (val <= transFuncData[0].value) {
			weatherColors[dataStep + 0] = transFuncData[0].R;
			weatherColors[dataStep + 1] = transFuncData[0].G;
			weatherColors[dataStep + 2] = transFuncData[0].B;
			#ifdef HIDE_NEGLIGIBLE_DATA
			weatherColors[dataStep + 3] = NEGLIGIBLE_TRANSPARENCY;
			#else
			weatherColors[dataStep + 3] = transparency;
			#endif
			continue;
		}
		/*
		#ifdef HIDE_NEGLIGIBLE_DATA
		else if (val < transFuncData[1].value) {
			weatherColors[dataStep + 0] = transFuncData[1].R;
			weatherColors[dataStep + 1] = transFuncData[1].G;
			weatherColors[dataStep + 2] = transFuncData[1].B;
			weatherColors[dataStep + 3] = NEGLIGIBLE_TRANSPARENCY;
			continue;
		}
		#endif
		// */
		else if (val >= transFuncData[tfsize - 1].value) {
			weatherColors[dataStep + 0] = transFuncData[tfsize - 1].R;
			weatherColors[dataStep + 1] = transFuncData[tfsize - 1].G;
			weatherColors[dataStep + 2] = transFuncData[tfsize - 1].B;
			weatherColors[dataStep + 3] = transparency;
			continue;
		}
		
		// calculate the color using transfer function
		int colorIndex;
		// find which interval this value lies in
		for (colorIndex = 0; colorIndex < tfsize; colorIndex++) {
			if (transFuncData[colorIndex].value > val) break;
		}
		
		// get lower color data
		GLubyte red1 = transFuncData[colorIndex - 1].R;
		GLubyte green1 = transFuncData[colorIndex - 1].G;
		GLubyte blue1 = transFuncData[colorIndex - 1].B;
		float lowVal = transFuncData[colorIndex - 1].value;
		
		// get upper color data
		GLubyte red2 = transFuncData[colorIndex].R;
		GLubyte green2 = transFuncData[colorIndex].G;
		GLubyte blue2 = transFuncData[colorIndex].B;
		float highVal = transFuncData[colorIndex].value;
		
		float diff = highVal - lowVal;

		// linearly interpolate the new color
		GLubyte newRed = (1.0 - ((val - lowVal)/diff))*red1 + (1.0 - ((highVal - val)/diff))*red2;
		GLubyte newGreen = (1.0 - ((val - lowVal)/diff))*green1 + (1.0 - ((highVal - val)/diff))*green2;
		GLubyte newBlue = (1.0 - ((val - lowVal)/diff))*blue1 + (1.0 - ((highVal - val)/diff))*blue2;

		// save the color
		weatherColors[dataStep + 0] = newRed;
		weatherColors[dataStep + 1] = newGreen;
		weatherColors[dataStep + 2] = newBlue;
		weatherColors[dataStep + 3] = transparency;
	}
	return;
}

void computeDailyColors(GLubyte *weatherColors, int wcsize, float *data, int dataSize) {
	if (wcsize != 4 * recSize && wcsize != 4 * totalSliceSteps) {
		unreachable("computeDailyColors");
		return;
	}

	float current;
	trans_t highMax, highMin, lowMax, lowMin;
	trans_t none = {128, 128, 128};

	// TODO: change this to take input from file
	if (weatherAttrNum == SNOWPACK_DAILY) {
		setTrans(highMax, 0, 0, 255);
		setTrans(highMin, 200, 200, 255);
		setTrans(lowMax, 255, 0, 0);
		setTrans(lowMin, 200, 128, 128);
	}
	else if (weatherAttrNum == SNOWFALL_DAILY) {
		setTrans(highMax, 255, 0, 255);
		setTrans(highMin, 170, 85, 170);
		setTrans(lowMax, 0, 0, 0);
		setTrans(lowMin, 0, 0, 0);
	}
	else if (weatherAttrNum == PRECIPITATION_DAILY) {
		setTrans(highMax, 255, 255, 255);
		setTrans(highMin, 85, 0, 85);
		setTrans(lowMax, 0, 0, 0);
		setTrans(lowMin, 0, 0, 0);
	}
	else if (weatherAttrNum == RUNOFF_DAILY) {
		setTrans(highMax, 255, 0, 255);
		setTrans(highMin, 255, 128, 255);
		setTrans(lowMax, 0, 0, 0);
		setTrans(lowMin, 0, 0, 0);
	}
	else {
		unreachable("computeDailyColors");
		return;
	}

	// get the min/max
	float min = weatherAttrMin[weatherAttrNum];
	float max = weatherAttrMax[weatherAttrNum];

	for (int dataStep = 0; dataStep < wcsize; dataStep += 4) {
		#ifndef SLICE_COLOR_ON
		// only do this for the slice graph
		if (wcsize == 4 * totalSliceSteps) {
			// make the line white
			weatherColors[dataStep + 0] = 255;
			weatherColors[dataStep + 1] = 255;
			weatherColors[dataStep + 2] = 255;
			weatherColors[dataStep + 3] = 0;
			continue;
		}
		#endif

		long totalOffset = (currentTimeStep * dataSize) + (dataStep / 4) - 1;

		float accumulated = data[totalOffset];

		if (currentTimeStep == 0) current = 0.0;
		// subtract the total accumulated from the last timestep's accumulated to get daily
		else current = accumulated - data[totalOffset - dataSize];

		float highSpan = max - EPSILON;
		float lowSpan = -(min + EPSILON);

		// if the difference was negligible
		if (current >= -EPSILON && current <= EPSILON) {
			weatherColors[dataStep + 0] = none.R;
			weatherColors[dataStep + 1] = none.G;
			weatherColors[dataStep + 2] = none.B;
			#ifdef HIDE_NEGLIGIBLE_DATA
			weatherColors[dataStep + 3] = NEGLIGIBLE_TRANSPARENCY;
			#else
			weatherColors[dataStep + 3] = transparency;
			#endif
		}
		// positive value
		else if (current > EPSILON) {
			GLubyte newRed = (1.0 - (current/highSpan))*highMin.R +
				(1.0 - ((max - current)/highSpan))*highMax.R;
			GLubyte newGreen = (1.0 - (current/highSpan))*highMin.G +
				(1.0 - ((max - current)/highSpan))*highMax.G;
			GLubyte newBlue = (1.0 - (current/highSpan))*highMin.B +
				(1.0 - ((max - current)/highSpan))*highMax.B;

			weatherColors[dataStep + 0] = newRed;
			weatherColors[dataStep + 1] = newGreen;
			weatherColors[dataStep + 2] = newBlue;
			weatherColors[dataStep + 3] = transparency;
		}
		// negative value
		else if (current < -EPSILON) {
			GLubyte newRed = (1.0 - (current/lowSpan))*lowMin.R +
				(1.0 - ((min - current)/lowSpan))*lowMax.R;
			GLubyte newGreen = (1.0 - (current/lowSpan))*lowMin.G +
				(1.0 - ((min - current)/lowSpan))*lowMax.G;
			GLubyte newBlue = (1.0 - (current/lowSpan))*lowMin.B +
				(1.0 - ((min - current)/lowSpan))*lowMax.B;

			weatherColors[dataStep + 0] = newRed;
			weatherColors[dataStep + 1] = newGreen;
			weatherColors[dataStep + 2] = newBlue;
			weatherColors[dataStep + 3] = transparency;
		}
		else {
			unreachable("computeDailyColors");
		}
	}
	return;
}

void computeSliceCoords(float *sliceCoords, int cosize, float *sliceData, float *prevSliceData) {
	#ifdef DEBUG2
	printf("computeSliceCoords()\n");
	#endif
	int numXSteps = cosize / 2;
	for (int i = 0; i < numXSteps; i++) {
		// x
		sliceCoords[2*i] = ((float)i / numXSteps) * SLICE_GRAPH_WIDTH;

		float scalingFactor = SLICE_GRAPH_HEIGHT / weatherAttrMax[weatherAttrNum];
		float foo = scalingFactor * sliceData[i];

		if (weatherAttrNum >= ATTR_MIN && weatherAttrNum < 4) {
			// use the accumulated value
		}
		else if (weatherAttrNum >= 4 && weatherAttrNum <= ATTR_MAX) {
			// use the difference in values
			float previous;
			if (currentTimeStep == 0) previous = 0.0;
			else previous = scalingFactor * prevSliceData[i]; 
			foo -= previous;
		}
		else unreachable("computeSliceCoords");

		// y
		sliceCoords[2*i + 1] = foo;
	}
	return;
}

void allocateWeatherDataSpace(NcFile *ncF) {
	// SNOW: snowpack
	NcVar *snowVar = ncF->get_var("SNOW");
	long snowRecSize = snowVar->rec_size();
	NcValues *snowVals = snowVar->get_rec();
	const long snowRecByteSize = snowVals->num() * snowVals->bytes_for_one();
	long snowByteSize = numNcFiles * timeSize * snowRecSize;
	snowpackData = new float[snowByteSize];
	
	// SNOWNC: snowfall
	NcVar *snowncVar = ncF->get_var("SNOWNC");
	long snowncRecSize = snowncVar->rec_size();
	NcValues *snowncVals = snowncVar->get_rec();
	long snowncRecByteSize = snowncVals->num() * snowncVals->bytes_for_one();
	long snowncByteSize = numNcFiles * timeSize * snowncRecSize;
	snowfallData = new float[snowncByteSize];

	#ifdef DEBUG2
	cout << "snowRecSize = " << snowRecSize << endl;
	cout << "snowRecByteSize = " << snowRecByteSize << endl;
	cout << "snowpack ByteSize = " << snowByteSize << endl;
	cout << "snowncRecSize = " << snowncRecSize << endl;
	cout << "snowncRecByteSize = " << snowncRecByteSize << endl;
	cout << "snowfall ByteSize = " << snowncByteSize << endl;
	#endif
	
	// RAINC+RAINNC: precipitation
	NcVar *raincVar = ncF->get_var("RAINC");
	NcVar *rainncVar = ncF->get_var("RAINNC");
	long raincRecSize = raincVar->rec_size();
	long rainncRecSize = rainncVar->rec_size();
	NcValues *raincVals = raincVar->get_rec();
	NcValues *rainncVals = rainncVar->get_rec();
	long raincRecByteSize = raincVals->num() * raincVals->bytes_for_one();
	long rainncRecByteSize = rainncVals->num() * rainncVals->bytes_for_one();
	long raincByteSize = numNcFiles * timeSize * raincRecSize;
	long rainncByteSize = numNcFiles * timeSize * rainncRecSize;
	precipitationData = new float[raincByteSize];
	#ifdef DEBUG2
	cout << "raincRecSize = " << raincRecSize << endl;
	cout << "raincRecByteSize = " << raincRecByteSize << endl;
	cout << "rainncRecSize = " << rainncRecSize << endl;
	cout << "rainncRecByteSize = " << rainncRecByteSize << endl;
	cout << "precipitation ByteSize = " << raincByteSize << endl;
	#endif
	
	// SFROFF+UDROFF: runoff
	NcVar *sfroffVar = ncF->get_var("SFROFF");
	NcVar *udroffVar = ncF->get_var("UDROFF");
	long sfroffRecSize = sfroffVar->rec_size();
	long udroffRecSize = udroffVar->rec_size();
	NcValues *sfroffVals = sfroffVar->get_rec();
	NcValues *udroffVals = udroffVar->get_rec();
	long sfroffRecByteSize = sfroffVals->num() * sfroffVals->bytes_for_one();
	long udroffRecByteSize = udroffVals->num() * udroffVals->bytes_for_one();
	long sfroffByteSize = numNcFiles * timeSize * sfroffRecSize;
	long udroffByteSize = numNcFiles * timeSize * udroffRecSize;
	runoffData = new float[sfroffByteSize];

	#ifdef DEBUG2
	cout << "sfroffRecSize = " << sfroffRecSize << endl;
	cout << "udroffRecByteSize = " << udroffRecByteSize << endl;
	cout << "sfroffRecSize = " << sfroffRecSize << endl;
	cout << "udroffRecByteSize = " << udroffRecByteSize << endl;
	cout << "runoff ByteSize = " << sfroffByteSize << endl;
	#endif
	return;
}

void precomputeWeatherParameters(NcFile *ncF) {
	#ifdef CONSOLE_OUTPUT
	cout << "Precomputing weather index data." << endl;
	#endif

	// START
	NcDim *uDim = ncF->rec_dim();
	NcVar *xVar = ncF->get_var("XLONG");
	NcVar *yVar = ncF->get_var("XLAT");
	timeSize = uDim->size();
	recSize = xVar->rec_size(); //  recSize == yRecSize
	long yRecSize = yVar->rec_size();
	NcValues *xVals = xVar->get_rec();
	NcValues *yVals = yVar->get_rec();
	// Size of record (timestep) array at xVals->base()
	// = xVals->num() * xVals->bytes_for_one()
	long xRecByteSize = xVals->num() * xVals->bytes_for_one();
	long yRecByteSize = yVals->num() * yVals->bytes_for_one();
	
	totalTimeSteps = numNcFiles * timeSize;
	long coordByteSize = (xRecByteSize + yRecByteSize);

	#ifdef DEBUG2
	cout << "timeSize = " << timeSize << endl;
	cout << "recSize = " << recSize << endl;
	cout << "yRecSize = " << yRecSize << endl;
	cout << "xRecByteSize = " << xRecByteSize << endl;
	cout << "yRecByteSize = " << yRecByteSize << endl;
	cout << "totalTimeSteps = " << totalTimeSteps << endl;
	cout << "coordByteSize = " << coordByteSize << endl;
	#endif

	coord_t first, previous, current;
	float dist1, dist2;
	// Dynamically find numRows and numCols by comparing the first point to
	// current and previous. If the distance between the current point and the
	// first point is small compared to the distance from the previous point,
	// the current point is the first point of a new row.
	for (int i = 0; i < MAX_INT; i++) {
		// initialize the first coord
		if (i == 0) {
			first.x = xVals->as_float(i);
			first.y = yVals->as_float(i);
			current.x = xVals->as_float(i);
			current.y = yVals->as_float(i);
			continue;
		}
		previous.x = current.x;
		previous.y = current.y;
		current.x = xVals->as_float(i);
		current.y = yVals->as_float(i);

		// get the distance from the current point to the previous point
		dist1 = sqrt(powf(current.x - previous.x, 2) + powf(current.y - previous.y, 2));
		// get the distance from the current point to the first point
		dist2 = sqrt(powf(current.x - first.x, 2) + powf(current.y - first.y, 2));

		// if the current point is much closer to the first point than the previous
		// it's the first point of a new row
		if (dist1 > 2 * dist2) {
			numCols = i;
			numRows = recSize / numCols;
			// error checking
			if (recSize % numCols != 0 || recSize % numRows != 0) {
				#ifndef ERROR_NOTIFICATION_OFF
				printf("ERROR: numCols or numRows computed incorrectly. Aborting\n");
				#endif
				exit(1);
			}
			#ifdef DEBUG2
			printf("numCols set to %d numRows set to %d\n", numCols, numRows);
			#endif
			break;
		}
	}

	// allocate space for weatherCoords
	if (weatherCoords == NULL) weatherCoords = new float[2 * recSize];

	// get the latitude and longitude of each data point
	for (int i = 0; i < recSize; i++) {
		float x = xVals->as_float(i);
		float y = yVals->as_float(i);
		// the coords are interleaved for rendering
		weatherCoords[2 * i] = x;
		weatherCoords[2 * i + 1] = y;

		// update min/max as necessary
		if (x > xMax) xMax = x;
		else if (x < xMin) xMin = x;

		if (y > yMax) yMax = y;
		else if (y < yMin) yMin = y;
	}

	// center the view
	xMid = (xMax + xMin) / 2.0;
	yMid = (yMax + yMin) / 2.0;
	
	eye[0] = xMid;
	eye[1] = yMid;
	// default zoom
	eye[2] = 20;
	
	#ifdef DEBUG2
	cout << "xMin = " << xMin << ", xMax = " << xMax << endl;
	cout << "yMin = " << yMin << ", yMax = " << yMax << endl;
	cout << "xMid = " << xMid << ", yMid = " << yMid << endl;
	#endif
	// END

	// precompute the index data
	for (int currRow = 0; currRow < numRows; currRow++) {
		weatherIndices.push_back(gluintVector);
		for (int currCol = 0; currCol < numCols; currCol++) {
			// precompute the index data only once
			weatherIndices[currRow].push_back((GLuint)(currRow * numCols + currCol));
			weatherIndices[currRow].push_back((GLuint)((currRow+1) * numCols + currCol));
		}
	}

	// precompute the weather outline data
	int rectSize = numCols * numRows;
	// first row
	for (int i = 0; i < numCols; i++) {
		// location
		weatherOutline.push_back(weatherCoords[i * 2]);
		weatherOutline.push_back(weatherCoords[i * 2 + 1]);
		weatherOutline.push_back(0.0);
		// color green
		weatherOutline.push_back(0.0);
		weatherOutline.push_back(1.0);
		weatherOutline.push_back(0.0);
	}
	// right side (last column)
	for (int i = numCols - 1; i < rectSize; i += numCols) {
		// location
		weatherOutline.push_back(weatherCoords[i * 2]);
		weatherOutline.push_back(weatherCoords[i * 2 + 1]);
		weatherOutline.push_back(0.0);
		// color green
		weatherOutline.push_back(0.0);
		weatherOutline.push_back(1.0);
		weatherOutline.push_back(0.0);
	}
	// top row, from right to left
	for (int i = rectSize - 1; i > rectSize - numCols; i--) {
		// location
		weatherOutline.push_back(weatherCoords[i * 2]);
		weatherOutline.push_back(weatherCoords[i * 2 + 1]);
		weatherOutline.push_back(0.0);
		// color green
		weatherOutline.push_back(0.0);
		weatherOutline.push_back(1.0);
		weatherOutline.push_back(0.0);
	}
	// left side (first column) from top to bottom
	for (int i = rectSize - numCols; i > 0; i -= numCols) {
		// location
		weatherOutline.push_back(weatherCoords[i * 2]);
		weatherOutline.push_back(weatherCoords[i * 2 + 1]);
		weatherOutline.push_back(0.0);
		// color green
		weatherOutline.push_back(0.0);
		weatherOutline.push_back(1.0);
		weatherOutline.push_back(0.0);
	}
	return;
}

void computeMaxsAndMins(void) {
	// update min/max as necessary
	for (int i = 0; i < totalTimeSteps; i++) {
		long timeStepOffset = i * recSize;
		for (int j = 0; j < recSize; j++) {
			long totalOffset = timeStepOffset + j;

			float val;

			val = snowpackData[totalOffset];
			if (val > weatherAttrMax[SNOWPACK]) weatherAttrMax[SNOWPACK] = val;
			if (val < weatherAttrMin[SNOWPACK]) weatherAttrMin[SNOWPACK] = val;

			val = snowfallData[totalOffset];
			if (val > weatherAttrMax[SNOWFALL]) weatherAttrMax[SNOWFALL] = val;
			if (val < weatherAttrMin[SNOWFALL]) weatherAttrMin[SNOWFALL] = val;
			
			val = precipitationData[totalOffset];
			if (val > weatherAttrMax[PRECIPITATION]) weatherAttrMax[PRECIPITATION] = val;
			if (val < weatherAttrMin[PRECIPITATION]) weatherAttrMin[PRECIPITATION] = val;
			
			val = runoffData[totalOffset];
			if (val > weatherAttrMax[RUNOFF]) weatherAttrMax[RUNOFF] = val;
			if (val < weatherAttrMin[RUNOFF]) weatherAttrMin[RUNOFF] = val;

			float delta;

			if (i == 0) delta = 0.0;
			else delta = snowpackData[totalOffset] - snowpackData[totalOffset - recSize];

			if (delta > weatherAttrMax[SNOWPACK_DAILY]) weatherAttrMax[SNOWPACK_DAILY] = delta;
			if (delta < weatherAttrMin[SNOWPACK_DAILY]) weatherAttrMin[SNOWPACK_DAILY] = delta;

			delta = snowfallData[totalOffset] - snowfallData[totalOffset - recSize];
			if (delta > weatherAttrMax[SNOWFALL_DAILY]) weatherAttrMax[SNOWFALL_DAILY] = delta;
			if (delta < weatherAttrMin[SNOWFALL_DAILY]) weatherAttrMin[SNOWFALL_DAILY] = delta;
			
			delta = precipitationData[totalOffset] - precipitationData[totalOffset - recSize];
			if (delta > weatherAttrMax[PRECIPITATION_DAILY]) weatherAttrMax[PRECIPITATION_DAILY] = delta;
			if (delta < weatherAttrMin[PRECIPITATION_DAILY]) weatherAttrMin[PRECIPITATION_DAILY] = delta;
			
			delta = runoffData[totalOffset] - runoffData[totalOffset - recSize];
			if (delta > weatherAttrMax[RUNOFF_DAILY]) weatherAttrMax[RUNOFF_DAILY] = delta;
			if (delta < weatherAttrMin[RUNOFF_DAILY]) weatherAttrMin[RUNOFF_DAILY] = delta;
		}
	}

	#ifdef DEBUG2
	// print out mins/maxs
	cout << "snowpackMin = " << weatherAttrMin[SNOWPACK] << endl;
	cout << "snowpackMax = " << weatherAttrMax[SNOWPACK] << endl;
	cout << "snowfallMin = " << weatherAttrMin[SNOWFALL] << endl;
	cout << "snowfallMax = " << weatherAttrMax[SNOWFALL] << endl;
	cout << "precipitationMin = " << weatherAttrMin[PRECIPITATION] << endl;
	cout << "precipitationMax = " << weatherAttrMax[PRECIPITATION] << endl;
	cout << "runoffMin = " << weatherAttrMin[RUNOFF] << endl;
	cout << "runoffMax = " << weatherAttrMax[RUNOFF] << endl;
	cout << "snowpackDailyMin = " << weatherAttrMin[4] << endl;
	cout << "snowpackDailyMax = " << weatherAttrMax[4] << endl;
	cout << "snowfallDailyMin = " << weatherAttrMin[5] << endl;
	cout << "snowfallDailyMax = " << weatherAttrMax[5] << endl;
	cout << "precipitationDailyMin = " << weatherAttrMin[6] << endl;
	cout << "precipitationDailyMax = " << weatherAttrMax[6] << endl;
	cout << "runoffDailyMin = " << weatherAttrMin[7] << endl;
	cout << "runoffDailyMax = " << weatherAttrMax[7] << endl;
	#endif

	return;
}

void parseCSVfiles(char *locFileName, char *dataFileName) {
	string line, cell;
	int i, j;
	coord_t coord;

	// There's a problem with the line endings for the csv files
	// It's not recognizing ^M as newline
	// TODO: must modify source file to replace ^M with space then back again
	// in vim: ":%s/^vm/ /g" then ":%s/ /^vm/g" and it's fixed
	// "^vm" means you must hold ctrl+v while pressing m

	ifstream locStream(locFileName);

	i = 0;
	// get the location data
    while(getline(locStream, line)) {
		// handle the first line of entry decriptors separately
		if (i == 0) {
			i++;
			continue;
		}
        stringstream lineStream(line);

		j = 0;
		// get the entries one at a time
        while(getline(lineStream, cell, ',')) {
			#ifdef DEBUG2
            printf("j = %d cell = %s\n", j, cell.c_str());
			#endif

			if (j == 1) coord.y = atof(cell.c_str());
			else if (j == 2) coord.x = atof(cell.c_str());
			else if (j == 3) {
				#ifdef DRAWING_3D
				coord.z = atof(cell.c_str());
				#else
				coord.z = 0.0;
				#endif
			}
			else if (j != 0) {
				#ifndef ERROR_NOTIFICATION_OFF
				cerr << "Error: Coord has more than 3 values. " <<
						"Are you sure this is the location csv file?" << endl;
				#endif
				return;
			}
			j++;
        }

		// finished with this line of data
		csvCoords.push_back(coord);
		i++;
    }

	ifstream dataStream(dataFileName);

	i = 0;
	// get the snowpack data until EOF is reached
    while(getline(dataStream, line)) {
		vector<float> rowData;

		// skip the line with column names
		if (i == 0) {
			i++;
			continue;
		}

        stringstream lineStream(line);

		j = 0;
		// get the entries one at a time
        while(getline(lineStream, cell, ',')) {
			#ifdef DEBUG2
            printf("j = %d cell = %s\n", j, cell.c_str());
			#endif

			if (j == 0) {
				j++;
				continue;
			}
			// push data for this station
			float value = atof(cell.c_str());
			rowData.push_back(value);
			j++;
			// update min/max as necessary
			if (value < csvMin) csvMin = value;
			else if (value > csvMax) csvMax = value;
        }
		// push all the data for this timestep(day)
		csvData.push_back(rowData);
		i++;
	}
	return;
}

void parseTransferFile(char *fileName) {
	// try to open the file
	float value, R, G, B;
	trans_t transDatum;
	
	ifstream fin(fileName);

	// use default values on error
	if (!fin) {
		#ifdef CONSOLE_OUTPUT
		printf("Transfer function file \"%s\" not found. Using defaults.\n", fileName);
		#endif
		// default transparency to halfway
		transparency = 128;

		// pink for less than 10% of max
		setTrans(transDatum, 255, 0, 128, 100);
		transFuncData.push_back(transDatum);
		
		// black to green gradient for weather
		setTrans(transDatum, 0, 0, 0, 101);
		transFuncData.push_back(transDatum);
		setTrans(transDatum, 0, 255, 0, 1000);
		transFuncData.push_back(transDatum);
	}
	else {
		// get and set the transparency
		fin >> value;
		transparency = 255 * value;

		#ifdef CONSOLE_OUTPUT
		printf("Transparency set to %d/255\n", (int)transparency);
		printf("Transfer function values:\n");
		#endif

		// read input until EOF is reached
		while (fin >> value >> R >> G >> B) {
			#ifdef CONSOLE_OUTPUT
			printf("    value = %.2f color = (%.2f, %.2f, %.2f)\n", value, R, G, B);
			#endif

			setTrans(transDatum, 255*R, 255*G, 255*B, value);
			transFuncData.push_back(transDatum);
		}

		// make sure we know where the smallest and largest values are
		sort(transFuncData.begin(), transFuncData.end());
	}

	// make sure we have transfer function data
	if (transFuncData.size() < 2) {
		#ifndef ERROR_NOTIFICATION_OFF
		cerr << "Error: Transfer function data not initialized properly. Aborting" << endl;
		#endif
		exit(1);
	}
	return;
}

// this function uses the equations:
// c.x = (1-t)*a.x + t*b.x
// c.y = (1-t)*a.y + t*b.y
// it then solves for separate values of t and compares them
lineloc_t aboveOrBelowLine(coord_t a, coord_t b, coord_t c) {
	float tx, ty;

	if (a.x != b.x) tx = (c.x - a.x) / (b.x - a.x);
	// TODO: find good way to handle this case
	else return ON_LINE;
	if (a.y != b.y) ty = (c.y - a.y) / (b.y - a.y);
	else {
		if (c.y == a.y) return ON_LINE;
		else if (c.y < a.y) return BELOW_LINE;
		else if (c.y > a.y) return ABOVE_LINE;
		else unreachable("aboveOrBelowLine");
	}

	#ifdef DEBUG2
	printf("tx = %f ty = %f\n", tx, ty);
	#endif

	if (tx == ty) return ON_LINE;
	else if (a.y < b.y) {
		if (tx > ty) return BELOW_LINE;
		else if (tx < ty) return ABOVE_LINE;
	}
	else if (a.y > b.y) {
		if (tx < ty) return BELOW_LINE;
		else if (tx > ty) return ABOVE_LINE;
	}
	unreachable("aboveOrBelowLine");
	return ON_LINE;
}

void setTrans(trans_t &t, GLubyte R, GLubyte G, GLubyte B) {
	t.R	= R;
	t.G = G;
	t.B = B;
	return;
}

void setTrans(trans_t &t, GLubyte R, GLubyte G, GLubyte B, float val) {
	t.R	= R;
	t.G = G;
	t.B = B;
	t.value = val;
	return;
}

void setCoord(coord_t &c, float x, float y, float z) {
	c.x = x;
	c.y = y;
	c.z = z;
	return;
}

void setCoord(coord_t &c, float x, float y, float z, int val) {
	c.x = x;
	c.y = y;
	c.z = z;
	c.val = val;
	return;
}

void unreachable(char *funcName) {
	#ifndef ERROR_NOTIFICATION_OFF
	fprintf(stderr, "ERROR: Unreachable state reached in function %s()\n", funcName);
	#endif
	return;
}

// this function is run just before the program exits
void cleanUpMemory(void) {
	delete [] snowpackData;
	delete [] snowfallData;
	delete [] precipitationData;
	delete [] runoffData;
	delete [] textures;

	printf("Simulation Complete.\n");
	return;
}

int main(int argc, char **argv)
{
	// command line should be parsed by something tbd
	if (argc < 3) {
		#ifndef ERROR_NOTIFICATION_OFF
		cerr << "usage: ingest <datafiles> <shapefiles>" << endl;
		#endif
		exit(1);
	}
	int currArgNum = 1;

	// arg 1 is wildcarded list of files to ingest
	char **ncFileList;
	wordexp_t p;
	wordexp(argv[currArgNum], &p, 0);
	numNcFiles = p.we_wordc;
	ncFileList = p.we_wordv;

	#ifdef CONSOLE_OUTPUT
	printf("Processing %d Ncfiles total.\n", numNcFiles);
	#endif

	currArgNum++;
	// For now we are assuming that all the files matched are the same format - not good
	NcFile ncF(ncFileList[0]);
	// minimal checking
	if (!ncF.is_valid()) {
		fprintf(stderr, "Error: File %s is not valid. Aborting.\n", ncFileList[0]);
		exit(1);
	}

	// First file will do for reading the x/y coordinates and to determine
	// array size for reading the remaining files
	precomputeWeatherParameters(&ncF);

	allocateWeatherDataSpace(&ncF);
	
	parseTransferFile("transfer.txt");

	getNcFileData(ncFileList);

	char *shapeFileName;
	int error = 0;
	// get data from the shapefile(s)
	while (argv[currArgNum] != NULL) {
		shapeFileName = argv[currArgNum];
		// we want fileNum to start at zero
		error = getShapeFileData(currArgNum - 2, shapeFileName);
		// stop when there's no args or we hit a non-shapefile
		if (error == -1) break;
		currArgNum++;
	}
	
	// with all weather data cached, find the maxs and mins for all attributes
	computeMaxsAndMins();

	// OpenGL setup
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH | GLUT_MULTISAMPLE);
	glutInitWindowSize(screenWidth, screenHeight);
	mainWindow = glutCreateWindow("Weather Simulation");
	#ifdef FULLSCREEN
	glutFullScreen();
	#endif

	glutDisplayFunc(redraw);
	glutReshapeFunc(reshape);
	glutIdleFunc(animate);
	glutVisibilityFunc(vis);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);
	glutKeyboardFunc(key);
	glutSpecialFunc(specialKey);

	// for transparency
	glEnable(GL_BLEND); 
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	ilInit();
	iluInit();
	ilutInit();
	ilutRenderer(ILUT_OPENGL);
	
	// get the image file names
	int numImageFiles;
	char **imageFileList;
	wordexp_t foo;
	wordexp(argv[currArgNum], &foo, 0);
	numImageFiles = foo.we_wordc;
	imageFileList = foo.we_wordv;
	#ifdef CONSOLE_OUTPUT
	printf("Processing %d image files total:\n", numImageFiles);
	#endif

	// generate textures
	textures = new GLuint[numImageFiles];
	glGenTextures(numImageFiles, textures);
	// initialize the textures
	for (int fileNum = 0; fileNum < numImageFiles; fileNum++) {
		jpeg2texture(fileNum, imageFileList[fileNum]);
		parseImageLocation(imageFileList[fileNum]);
	}

	// advance to the next command line input
	currArgNum++;

	parseCSVfiles(argv[currArgNum + 1], argv[currArgNum]);

	// default is snowpack
	weatherData = snowpackData;
	weatherAttrNum = SNOWPACK;

	// clean up memory on exit
	atexit(cleanUpMemory);

	printf("Starting Simulation.\n");
	glutMainLoop();

	return 0;
}
