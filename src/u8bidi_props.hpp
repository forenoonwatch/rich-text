#pragma once

#include <unicode/ubidi.h>

/******************************************************************
 The Properties state machine table
*******************************************************************

 All table cells are 8 bits:
	  bits 0..4:  next state
	  bits 5..7:  action to perform (if > 0)

 Cells may be of format "n" where n represents the next state
 (except for the rightmost column).
 Cells may also be of format "s(x,y)" where x represents an action
 to perform and y represents the next state.

*******************************************************************
 Definitions and type for properties state table
*******************************************************************
*/
#define IMPTABPROPS_COLUMNS 16
#define IMPTABPROPS_RES (IMPTABPROPS_COLUMNS - 1)
#define GET_STATEPROPS(cell) ((cell)&0x1f)
#define GET_ACTIONPROPS(cell) ((cell)>>5)
#define s(action, newState) ((uint8_t)(newState+(action<<5)))

static const uint8_t groupProp[] =		  /* dirProp regrouped */
{
/*  L   R   EN  ES  ET  AN  CS  B   S   WS  ON  LRE LRO AL  RLE RLO PDF NSM BN  FSI LRI RLI PDI ENL ENR */
	0,  1,  2,  7,  8,  3,  9,  6,  5,  4,  4,  10, 10, 12, 10, 10, 10, 11, 10, 4,  4,  4,  4,  13, 14
};
enum { DirProp_L=0, DirProp_R=1, DirProp_EN=2, DirProp_AN=3, DirProp_ON=4, DirProp_S=5, DirProp_B=6 }; /* reduced dirProp */

/******************************************************************

	  PROPERTIES  STATE  TABLE

 In table impTabProps,
	  - the ON column regroups ON and WS, FSI, RLI, LRI and PDI
	  - the BN column regroups BN, LRE, RLE, LRO, RLO, PDF
	  - the Res column is the reduced property assigned to a run

 Action 1: process current run1, init new run1
		2: init new run2
		3: process run1, process run2, init new run1
		4: process run1, set run1=run2, init new run2

 Notes:
  1) This table is used in resolveImplicitLevels().
  2) This table triggers actions when there is a change in the Bidi
	 property of incoming characters (action 1).
  3) Most such property sequences are processed immediately (in
	 fact, passed to processPropertySeq().
  4) However, numbers are assembled as one sequence. This means
	 that undefined situations (like CS following digits, until
	 it is known if the next char will be a digit) are held until
	 following chars define them.
	 Example: digits followed by CS, then comes another CS or ON;
			  the digits will be processed, then the CS assigned
			  as the start of an ON sequence (action 3).
  5) There are cases where more than one sequence must be
	 processed, for instance digits followed by CS followed by L:
	 the digits must be processed as one sequence, and the CS
	 must be processed as an ON sequence, all this before starting
	 assembling chars for the opening L sequence.


*/
static const uint8_t impTabProps[][IMPTABPROPS_COLUMNS] =
{
/*						L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B ,	ES ,	ET ,	CS ,	BN ,   NSM ,	AL ,   ENL ,   ENR , Res */
/* 0 Init		*/ {	 1 ,	 2 ,	 4 ,	 5 ,	 7 ,	15 ,	17 ,	 7 ,	 9 ,	 7 ,	 0 ,	 7 ,	 3 ,	18 ,	21 , DirProp_ON },
/* 1 L		   */ {	 1 , s(1,2), s(1,4), s(1,5), s(1,7),s(1,15),s(1,17), s(1,7), s(1,9), s(1,7),	 1 ,	 1 , s(1,3),s(1,18),s(1,21),  DirProp_L },
/* 2 R		   */ { s(1,1),	 2 , s(1,4), s(1,5), s(1,7),s(1,15),s(1,17), s(1,7), s(1,9), s(1,7),	 2 ,	 2 , s(1,3),s(1,18),s(1,21),  DirProp_R },
/* 3 AL		  */ { s(1,1), s(1,2), s(1,6), s(1,6), s(1,8),s(1,16),s(1,17), s(1,8), s(1,8), s(1,8),	 3 ,	 3 ,	 3 ,s(1,18),s(1,21),  DirProp_R },
/* 4 EN		  */ { s(1,1), s(1,2),	 4 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,10),	11 ,s(2,10),	 4 ,	 4 , s(1,3),	18 ,	21 , DirProp_EN },
/* 5 AN		  */ { s(1,1), s(1,2), s(1,4),	 5 , s(1,7),s(1,15),s(1,17), s(1,7), s(1,9),s(2,12),	 5 ,	 5 , s(1,3),s(1,18),s(1,21), DirProp_AN },
/* 6 AL:EN/AN	*/ { s(1,1), s(1,2),	 6 ,	 6 , s(1,8),s(1,16),s(1,17), s(1,8), s(1,8),s(2,13),	 6 ,	 6 , s(1,3),	18 ,	21 , DirProp_AN },
/* 7 ON		  */ { s(1,1), s(1,2), s(1,4), s(1,5),	 7 ,s(1,15),s(1,17),	 7 ,s(2,14),	 7 ,	 7 ,	 7 , s(1,3),s(1,18),s(1,21), DirProp_ON },
/* 8 AL:ON	   */ { s(1,1), s(1,2), s(1,6), s(1,6),	 8 ,s(1,16),s(1,17),	 8 ,	 8 ,	 8 ,	 8 ,	 8 , s(1,3),s(1,18),s(1,21), DirProp_ON },
/* 9 ET		  */ { s(1,1), s(1,2),	 4 , s(1,5),	 7 ,s(1,15),s(1,17),	 7 ,	 9 ,	 7 ,	 9 ,	 9 , s(1,3),	18 ,	21 , DirProp_ON },
/*10 EN+ES/CS	*/ { s(3,1), s(3,2),	 4 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),	10 , s(4,7), s(3,3),	18 ,	21 , DirProp_EN },
/*11 EN+ET	   */ { s(1,1), s(1,2),	 4 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),	11 , s(1,7),	11 ,	11 , s(1,3),	18 ,	21 , DirProp_EN },
/*12 AN+CS	   */ { s(3,1), s(3,2), s(3,4),	 5 , s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),	12 , s(4,7), s(3,3),s(3,18),s(3,21), DirProp_AN },
/*13 AL:EN/AN+CS */ { s(3,1), s(3,2),	 6 ,	 6 , s(4,8),s(3,16),s(3,17), s(4,8), s(4,8), s(4,8),	13 , s(4,8), s(3,3),	18 ,	21 , DirProp_AN },
/*14 ON+ET	   */ { s(1,1), s(1,2), s(4,4), s(1,5),	 7 ,s(1,15),s(1,17),	 7 ,	14 ,	 7 ,	14 ,	14 , s(1,3),s(4,18),s(4,21), DirProp_ON },
/*15 S		   */ { s(1,1), s(1,2), s(1,4), s(1,5), s(1,7),	15 ,s(1,17), s(1,7), s(1,9), s(1,7),	15 , s(1,7), s(1,3),s(1,18),s(1,21),  DirProp_S },
/*16 AL:S		*/ { s(1,1), s(1,2), s(1,6), s(1,6), s(1,8),	16 ,s(1,17), s(1,8), s(1,8), s(1,8),	16 , s(1,8), s(1,3),s(1,18),s(1,21),  DirProp_S },
/*17 B		   */ { s(1,1), s(1,2), s(1,4), s(1,5), s(1,7),s(1,15),	17 , s(1,7), s(1,9), s(1,7),	17 , s(1,7), s(1,3),s(1,18),s(1,21),  DirProp_B },
/*18 ENL		 */ { s(1,1), s(1,2),	18 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,19),	20 ,s(2,19),	18 ,	18 , s(1,3),	18 ,	21 ,  DirProp_L },
/*19 ENL+ES/CS   */ { s(3,1), s(3,2),	18 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),	19 , s(4,7), s(3,3),	18 ,	21 ,  DirProp_L },
/*20 ENL+ET	  */ { s(1,1), s(1,2),	18 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),	20 , s(1,7),	20 ,	20 , s(1,3),	18 ,	21 ,  DirProp_L },
/*21 ENR		 */ { s(1,1), s(1,2),	21 , s(1,5), s(1,7),s(1,15),s(1,17),s(2,22),	23 ,s(2,22),	21 ,	21 , s(1,3),	18 ,	21 , DirProp_AN },
/*22 ENR+ES/CS   */ { s(3,1), s(3,2),	21 , s(3,5), s(4,7),s(3,15),s(3,17), s(4,7),s(4,14), s(4,7),	22 , s(4,7), s(3,3),	18 ,	21 , DirProp_AN },
/*23 ENR+ET	  */ { s(1,1), s(1,2),	21 , s(1,5), s(1,7),s(1,15),s(1,17), s(1,7),	23 , s(1,7),	23 ,	23 , s(1,3),	18 ,	21 , DirProp_AN }
};

/*  we must undef macro s because the levels tables have a different
 *  structure (4 bits for action and 4 bits for next state.
 */
#undef s

/******************************************************************
 The levels state machine tables
*******************************************************************

 All table cells are 8 bits:
	  bits 0..3:  next state
	  bits 4..7:  action to perform (if > 0)

 Cells may be of format "n" where n represents the next state
 (except for the rightmost column).
 Cells may also be of format "s(x,y)" where x represents an action
 to perform and y represents the next state.

 This format limits each table to 16 states each and to 15 actions.

*******************************************************************
 Definitions and type for levels state tables
*******************************************************************
*/
#define IMPTABLEVELS_COLUMNS (DirProp_B + 2)
#define IMPTABLEVELS_RES (IMPTABLEVELS_COLUMNS - 1)
#define GET_STATE(cell) ((cell)&0x0f)
#define GET_ACTION(cell) ((cell)>>4)
#define s(action, newState) ((uint8_t)(newState+(action<<4)))

typedef uint8_t ImpTab[][IMPTABLEVELS_COLUMNS];
typedef uint8_t ImpAct[];

/* FOOD FOR THOUGHT: each ImpTab should have its associated ImpAct,
 * instead of having a pair of ImpTab and a pair of ImpAct.
 */
typedef struct ImpTabPair {
	const void * pImpTab[2];
	const void * pImpAct[2];
} ImpTabPair;

/******************************************************************

	  LEVELS  STATE  TABLES

 In all levels state tables,
	  - state 0 is the initial state
	  - the Res column is the increment to add to the text level
		for this property sequence.

 The impAct arrays for each table of a pair map the local action
 numbers of the table to the total list of actions. For instance,
 action 2 in a given table corresponds to the action number which
 appears in entry [2] of the impAct array for that table.
 The first entry of all impAct arrays must be 0.

 Action 1: init conditional sequence
		2: prepend conditional sequence to current sequence
		3: set ON sequence to new level - 1
		4: init EN/AN/ON sequence
		5: fix EN/AN/ON sequence followed by R
		6: set previous level sequence to level 2

 Notes:
  1) These tables are used in processPropertySeq(). The input
	 is property sequences as determined by resolveImplicitLevels.
  2) Most such property sequences are processed immediately
	 (levels are assigned).
  3) However, some sequences cannot be assigned a final level till
	 one or more following sequences are received. For instance,
	 ON following an R sequence within an even-level paragraph.
	 If the following sequence is R, the ON sequence will be
	 assigned basic run level+1, and so will the R sequence.
  4) S is generally handled like ON, since its level will be fixed
	 to paragraph level in adjustWSLevels().

*/

static const ImpTab impTabL_DEFAULT =   /* Even paragraph level */
/*  In this table, conditional sequences receive the lower possible level
	until proven otherwise.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 0 ,	 1 ,	 0 ,	 2 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : R		  */ {	 0 ,	 1 ,	 3 ,	 3 , s(1,4), s(1,4),	 0 ,  1 },
/* 2 : AN		 */ {	 0 ,	 1 ,	 0 ,	 2 , s(1,5), s(1,5),	 0 ,  2 },
/* 3 : R+EN/AN	*/ {	 0 ,	 1 ,	 3 ,	 3 , s(1,4), s(1,4),	 0 ,  2 },
/* 4 : R+ON	   */ {	 0 , s(2,1), s(3,3), s(3,3),	 4 ,	 4 ,	 0 ,  0 },
/* 5 : AN+ON	  */ {	 0 , s(2,1),	 0 , s(3,2),	 5 ,	 5 ,	 0 ,  0 }
};
static const ImpTab impTabR_DEFAULT =   /* Odd  paragraph level */
/*  In this table, conditional sequences receive the lower possible level
	until proven otherwise.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 1 ,	 0 ,	 2 ,	 2 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L		  */ {	 1 ,	 0 ,	 1 ,	 3 , s(1,4), s(1,4),	 0 ,  1 },
/* 2 : EN/AN	  */ {	 1 ,	 0 ,	 2 ,	 2 ,	 0 ,	 0 ,	 0 ,  1 },
/* 3 : L+AN	   */ {	 1 ,	 0 ,	 1 ,	 3 ,	 5 ,	 5 ,	 0 ,  1 },
/* 4 : L+ON	   */ { s(2,1),	 0 , s(2,1),	 3 ,	 4 ,	 4 ,	 0 ,  0 },
/* 5 : L+AN+ON	*/ {	 1 ,	 0 ,	 1 ,	 3 ,	 5 ,	 5 ,	 0 ,  0 }
};
static const ImpAct impAct0 = {0,1,2,3,4};
static const ImpTabPair impTab_DEFAULT = {{&impTabL_DEFAULT,
										   &impTabR_DEFAULT},
										  {&impAct0, &impAct0}};

static const ImpTab impTabL_NUMBERS_SPECIAL =   /* Even paragraph level */
/*  In this table, conditional sequences receive the lower possible level
	until proven otherwise.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 0 ,	 2 , s(1,1), s(1,1),	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L+EN/AN	*/ {	 0 , s(4,2),	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 2 : R		  */ {	 0 ,	 2 ,	 4 ,	 4 , s(1,3), s(1,3),	 0 ,  1 },
/* 3 : R+ON	   */ {	 0 , s(2,2), s(3,4), s(3,4),	 3 ,	 3 ,	 0 ,  0 },
/* 4 : R+EN/AN	*/ {	 0 ,	 2 ,	 4 ,	 4 , s(1,3), s(1,3),	 0 ,  2 }
};
static const ImpTabPair impTab_NUMBERS_SPECIAL = {{&impTabL_NUMBERS_SPECIAL,
												   &impTabR_DEFAULT},
												  {&impAct0, &impAct0}};

static const ImpTab impTabL_GROUP_NUMBERS_WITH_R =
/*  In this table, EN/AN+ON sequences receive levels as if associated with R
	until proven that there is L or sor/eor on both sides. AN is handled like EN.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 init		 */ {	 0 ,	 3 , s(1,1), s(1,1),	 0 ,	 0 ,	 0 ,  0 },
/* 1 EN/AN		*/ { s(2,0),	 3 ,	 1 ,	 1 ,	 2 , s(2,0), s(2,0),  2 },
/* 2 EN/AN+ON	 */ { s(2,0),	 3 ,	 1 ,	 1 ,	 2 , s(2,0), s(2,0),  1 },
/* 3 R			*/ {	 0 ,	 3 ,	 5 ,	 5 , s(1,4),	 0 ,	 0 ,  1 },
/* 4 R+ON		 */ { s(2,0),	 3 ,	 5 ,	 5 ,	 4 , s(2,0), s(2,0),  1 },
/* 5 R+EN/AN	  */ {	 0 ,	 3 ,	 5 ,	 5 , s(1,4),	 0 ,	 0 ,  2 }
};
static const ImpTab impTabR_GROUP_NUMBERS_WITH_R =
/*  In this table, EN/AN+ON sequences receive levels as if associated with R
	until proven that there is L on both sides. AN is handled like EN.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 init		 */ {	 2 ,	 0 ,	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 EN/AN		*/ {	 2 ,	 0 ,	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  1 },
/* 2 L			*/ {	 2 ,	 0 , s(1,4), s(1,4), s(1,3),	 0 ,	 0 ,  1 },
/* 3 L+ON		 */ { s(2,2),	 0 ,	 4 ,	 4 ,	 3 ,	 0 ,	 0 ,  0 },
/* 4 L+EN/AN	  */ { s(2,2),	 0 ,	 4 ,	 4 ,	 3 ,	 0 ,	 0 ,  1 }
};
static const ImpTabPair impTab_GROUP_NUMBERS_WITH_R = {
						{&impTabL_GROUP_NUMBERS_WITH_R,
						 &impTabR_GROUP_NUMBERS_WITH_R},
						{&impAct0, &impAct0}};


static const ImpTab impTabL_INVERSE_NUMBERS_AS_L =
/*  This table is identical to the Default LTR table except that EN and AN are
	handled like L.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 0 ,	 1 ,	 0 ,	 0 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : R		  */ {	 0 ,	 1 ,	 0 ,	 0 , s(1,4), s(1,4),	 0 ,  1 },
/* 2 : AN		 */ {	 0 ,	 1 ,	 0 ,	 0 , s(1,5), s(1,5),	 0 ,  2 },
/* 3 : R+EN/AN	*/ {	 0 ,	 1 ,	 0 ,	 0 , s(1,4), s(1,4),	 0 ,  2 },
/* 4 : R+ON	   */ { s(2,0),	 1 , s(2,0), s(2,0),	 4 ,	 4 , s(2,0),  1 },
/* 5 : AN+ON	  */ { s(2,0),	 1 , s(2,0), s(2,0),	 5 ,	 5 , s(2,0),  1 }
};
static const ImpTab impTabR_INVERSE_NUMBERS_AS_L =
/*  This table is identical to the Default RTL table except that EN and AN are
	handled like L.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 1 ,	 0 ,	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L		  */ {	 1 ,	 0 ,	 1 ,	 1 , s(1,4), s(1,4),	 0 ,  1 },
/* 2 : EN/AN	  */ {	 1 ,	 0 ,	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  1 },
/* 3 : L+AN	   */ {	 1 ,	 0 ,	 1 ,	 1 ,	 5 ,	 5 ,	 0 ,  1 },
/* 4 : L+ON	   */ { s(2,1),	 0 , s(2,1), s(2,1),	 4 ,	 4 ,	 0 ,  0 },
/* 5 : L+AN+ON	*/ {	 1 ,	 0 ,	 1 ,	 1 ,	 5 ,	 5 ,	 0 ,  0 }
};
static const ImpTabPair impTab_INVERSE_NUMBERS_AS_L = {
						{&impTabL_INVERSE_NUMBERS_AS_L,
						 &impTabR_INVERSE_NUMBERS_AS_L},
						{&impAct0, &impAct0}};

static const ImpTab impTabR_INVERSE_LIKE_DIRECT =   /* Odd  paragraph level */
/*  In this table, conditional sequences receive the lower possible level
	until proven otherwise.
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 1 ,	 0 ,	 2 ,	 2 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L		  */ {	 1 ,	 0 ,	 1 ,	 2 , s(1,3), s(1,3),	 0 ,  1 },
/* 2 : EN/AN	  */ {	 1 ,	 0 ,	 2 ,	 2 ,	 0 ,	 0 ,	 0 ,  1 },
/* 3 : L+ON	   */ { s(2,1), s(3,0),	 6 ,	 4 ,	 3 ,	 3 , s(3,0),  0 },
/* 4 : L+ON+AN	*/ { s(2,1), s(3,0),	 6 ,	 4 ,	 5 ,	 5 , s(3,0),  3 },
/* 5 : L+AN+ON	*/ { s(2,1), s(3,0),	 6 ,	 4 ,	 5 ,	 5 , s(3,0),  2 },
/* 6 : L+ON+EN	*/ { s(2,1), s(3,0),	 6 ,	 4 ,	 3 ,	 3 , s(3,0),  1 }
};
static const ImpAct impAct1 = {0,1,13,14};
/* FOOD FOR THOUGHT: in LTR table below, check case "JKL 123abc"
 */
static const ImpTabPair impTab_INVERSE_LIKE_DIRECT = {
						{&impTabL_DEFAULT,
						 &impTabR_INVERSE_LIKE_DIRECT},
						{&impAct0, &impAct1}};

static const ImpTab impTabL_INVERSE_LIKE_DIRECT_WITH_MARKS =
/*  The case handled in this table is (visually):  R EN L
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 0 , s(6,3),	 0 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L+AN	   */ {	 0 , s(6,3),	 0 ,	 1 , s(1,2), s(3,0),	 0 ,  4 },
/* 2 : L+AN+ON	*/ { s(2,0), s(6,3), s(2,0),	 1 ,	 2 , s(3,0), s(2,0),  3 },
/* 3 : R		  */ {	 0 , s(6,3), s(5,5), s(5,6), s(1,4), s(3,0),	 0 ,  3 },
/* 4 : R+ON	   */ { s(3,0), s(4,3), s(5,5), s(5,6),	 4 , s(3,0), s(3,0),  3 },
/* 5 : R+EN	   */ { s(3,0), s(4,3),	 5 , s(5,6), s(1,4), s(3,0), s(3,0),  4 },
/* 6 : R+AN	   */ { s(3,0), s(4,3), s(5,5),	 6 , s(1,4), s(3,0), s(3,0),  4 }
};
static const ImpTab impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS =
/*  The cases handled in this table are (visually):  R EN L
													 R L AN L
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ { s(1,3),	 0 ,	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : R+EN/AN	*/ { s(2,3),	 0 ,	 1 ,	 1 ,	 2 , s(4,0),	 0 ,  1 },
/* 2 : R+EN/AN+ON */ { s(2,3),	 0 ,	 1 ,	 1 ,	 2 , s(4,0),	 0 ,  0 },
/* 3 : L		  */ {	 3 ,	 0 ,	 3 , s(3,6), s(1,4), s(4,0),	 0 ,  1 },
/* 4 : L+ON	   */ { s(5,3), s(4,0),	 5 , s(3,6),	 4 , s(4,0), s(4,0),  0 },
/* 5 : L+ON+EN	*/ { s(5,3), s(4,0),	 5 , s(3,6),	 4 , s(4,0), s(4,0),  1 },
/* 6 : L+AN	   */ { s(5,3), s(4,0),	 6 ,	 6 ,	 4 , s(4,0), s(4,0),  3 }
};
static const ImpAct impAct2 = {0,1,2,5,6,7,8};
static const ImpAct impAct3 = {0,1,9,10,11,12};
static const ImpTabPair impTab_INVERSE_LIKE_DIRECT_WITH_MARKS = {
						{&impTabL_INVERSE_LIKE_DIRECT_WITH_MARKS,
						 &impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS},
						{&impAct2, &impAct3}};

static const ImpTabPair impTab_INVERSE_FOR_NUMBERS_SPECIAL = {
						{&impTabL_NUMBERS_SPECIAL,
						 &impTabR_INVERSE_LIKE_DIRECT},
						{&impAct0, &impAct1}};

static const ImpTab impTabL_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS =
/*  The case handled in this table is (visually):  R EN L
*/
{
/*						 L ,	 R ,	EN ,	AN ,	ON ,	 S ,	 B , Res */
/* 0 : init	   */ {	 0 , s(6,2),	 1 ,	 1 ,	 0 ,	 0 ,	 0 ,  0 },
/* 1 : L+EN/AN	*/ {	 0 , s(6,2),	 1 ,	 1 ,	 0 , s(3,0),	 0 ,  4 },
/* 2 : R		  */ {	 0 , s(6,2), s(5,4), s(5,4), s(1,3), s(3,0),	 0 ,  3 },
/* 3 : R+ON	   */ { s(3,0), s(4,2), s(5,4), s(5,4),	 3 , s(3,0), s(3,0),  3 },
/* 4 : R+EN/AN	*/ { s(3,0), s(4,2),	 4 ,	 4 , s(1,3), s(3,0), s(3,0),  4 }
};
static const ImpTabPair impTab_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS = {
						{&impTabL_INVERSE_FOR_NUMBERS_SPECIAL_WITH_MARKS,
						 &impTabR_INVERSE_LIKE_DIRECT_WITH_MARKS},
						{&impAct2, &impAct3}};

#undef s

typedef struct {
	const ImpTab * pImpTab;			 /* level table pointer		  */
	const ImpAct * pImpAct;			 /* action map array			 */
	int32_t startON;					/* start of ON sequence		 */
	int32_t startL2EN;				  /* start of level 2 sequence	*/
	int32_t lastStrongRTL;			  /* index of last found R or AL  */
	int32_t state;					  /* current state				*/
	int32_t runStart;				   /* start position of the run	*/
	UBiDiLevel runLevel;				/* run level before implicit solving */
} LevState;

