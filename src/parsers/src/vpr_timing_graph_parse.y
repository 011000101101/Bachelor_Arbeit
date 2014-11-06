%{

#include <stdio.h>
#include <cassert>
#include <cstring>
#include <string>
#include <cmath>

#include "vpr_timing_graph_parse_common.h"

int yyerror(const char *msg);
extern int yylex(void);
extern int yylineno;
extern char* yytext;

%}

%union {
    char* strVal;
    double floatVal;
    int intVal;
    pin_blk_t pinBlkVal;
    domain_skew_iodelay_t domainSkewIodelayVal;
    edge_t edgeVal;
    node_arr_req_t nodeArrReqVal;
}

/* Verbose error reporting */
%error-verbose

/* declare constant tokens */
%token TGRAPH_HEADER          "timing_graph_header"
%token NUM_TNODES             "num_tnodes:"
%token NUM_TNODE_LEVELS       "num_tnode_levels:"
%token LEVEL                  "Level:"
%token NUM_LEVEL_NODES        "Num_nodes:"
%token NODES                  "Nodes:"
%token NET_DRIVER_TNODE_HEADER "Net #\tNet_to_driver_tnode"
%token NODE_ARR_REQ_HEADER    "node_req_arr_header"

%token TN_INPAD_SOURCE        "TN_INPAD_SOURCE"
%token TN_INPAD_OPIN          "TN_INPAD_OPIN"
%token TN_OUTPAD_IPIN         "TN_OUTPAD_IPIN"
%token TN_OUTPAD_SINK         "TN_OUTPAD_SINK"
%token TN_CB_IPIN             "TN_CB_IPIN"
%token TN_CB_OPIN             "TN_CB_OPIN"
%token TN_INTERMEDIATE_NODE   "TN_INTERMEDIATE_NODE"
%token TN_PRIMITIVE_IPIN      "TN_PRIMITIVE_IPIN"
%token TN_PRIMITIVE_OPIN      "TN_PRIMITIVE_OPIN"
%token TN_FF_IPIN             "TN_FF_IPIN"
%token TN_FF_OPIN             "TN_FF_OPIN"
%token TN_FF_SINK             "TN_FF_SINK"
%token TN_FF_SOURCE           "TN_FF_SOURCE"
%token TN_FF_CLOCK            "TN_FF_CLOCK"
%token TN_CLOCK_SOURCE        "TN_CLOCK_SOURCE"
%token TN_CLOCK_OPIN          "TN_CLOCK_OPIN"
%token TN_CONSTANT_GEN_SOURCE "TN_CONSTANT_GEN_SOURCE"

%token EOL "end-of-line"
%token TAB "tab-character"

/* declare variable tokens */
%token <floatVal> FLOAT_NUMBER
%token <intVal> INT_NUMBER

/* declare types */
%type <floatVal> number
%type <floatVal> float_number
%type <intVal> int_number

%type <intVal> num_tnodes
%type <intVal> tnode
%type <intVal> node_id
%type <intVal> num_out_edges
%type <intVal> num_tnode_levels

%type <strVal> tnode_type
%type <strVal> TN_INPAD_SOURCE       
%type <strVal> TN_INPAD_OPIN         
%type <strVal> TN_OUTPAD_IPIN        
%type <strVal> TN_OUTPAD_SINK        
%type <strVal> TN_CB_IPIN            
%type <strVal> TN_CB_OPIN            
%type <strVal> TN_INTERMEDIATE_NODE  
%type <strVal> TN_PRIMITIVE_IPIN     
%type <strVal> TN_PRIMITIVE_OPIN     
%type <strVal> TN_FF_IPIN            
%type <strVal> TN_FF_OPIN            
%type <strVal> TN_FF_SINK            
%type <strVal> TN_FF_SOURCE          
%type <strVal> TN_FF_CLOCK           
%type <strVal> TN_CLOCK_SOURCE       
%type <strVal> TN_CLOCK_OPIN         
%type <strVal> TN_CONSTANT_GEN_SOURCE

%type <pinBlkVal> pin_blk
%type <domainSkewIodelayVal> domain_skew_iodelay
%type <edgeVal> tedge
%type <nodeArrReqVal> node_arr_req_time
%type <floatVal> t_arr_req


/* Top level rule */
%start timing_graph 
%%

timing_graph: num_tnodes                    {printf("Timing Graph of %d nodes\n", $1);}
    | timing_graph TGRAPH_HEADER            {printf("Timing Graph file Header\n");}
    | timing_graph tnode                    {}
    | timing_graph num_tnode_levels         {printf("Timing Graph Levels %d\n", $2);}
    | timing_graph timing_graph_level       {printf("Adding TG level\n");}
    | timing_graph NET_DRIVER_TNODE_HEADER  {printf("Net to driver Tnode header\n");}
    | timing_graph NODE_ARR_REQ_HEADER EOL  {printf("Nodes ARR REQ Header\n");}
    | timing_graph node_arr_req_time        {printf("Adding Node Arr/Req Times\n");}
    | timing_graph EOL                      {}
    ;

tnode: node_id tnode_type pin_blk domain_skew_iodelay num_out_edges { printf("Node %d, Type %s, ipin %d, iblk %d, domain %d, skew %f, iodelay %f, edges %d\n", $1, $2, $3.ipin, $3.iblk, $4.domain, $4.skew, $4.iodelay, $5); }
    | tnode tedge {/*printf("edge to %d delay %e\n", $2.to_node, $2.delay);*/}
    ;

node_id: int_number TAB {$$ = $1;}
    ;

pin_blk: int_number TAB int_number TAB { $$.ipin = $1; $$.iblk = $3; }
    | TAB int_number TAB { $$.ipin = -1; $$.iblk = $2; }
    ;

domain_skew_iodelay: int_number TAB number TAB TAB { $$.domain = $1; $$.skew = $3; $$.iodelay = NAN; }
    | int_number TAB TAB number TAB { $$.domain = $1; $$.skew = NAN; $$.iodelay = $4; }
    | TAB TAB TAB TAB { $$.domain = -1; $$.skew = NAN; $$.iodelay = NAN; }
    ;

num_out_edges: int_number {$$ = $1;}
    ;

tedge: TAB int_number TAB number EOL { $$.to_node = $2; $$.delay = $4; }
    | TAB TAB TAB TAB TAB TAB TAB TAB TAB TAB int_number TAB float_number EOL { $$.to_node = $11; $$.delay = $13; }
    ;

tnode_type: TN_INPAD_SOURCE TAB     { $$ = strdup("TN_INPAD_SOURCE"); } 
    | TN_INPAD_OPIN TAB             { $$ = strdup("TN_INPAD_OPIN"); } 
    | TN_OUTPAD_IPIN TAB            { $$ = strdup("TN_OUTPAD_IPIN"); } 
    | TN_OUTPAD_SINK TAB            { $$ = strdup("TN_OUTPAD_SINK"); } 
    | TN_CB_IPIN TAB                { $$ = strdup("TN_CB_IPIN"); } 
    | TN_CB_OPIN TAB                { $$ = strdup("TN_CB_OPIN"); } 
    | TN_INTERMEDIATE_NODE TAB      { $$ = strdup("TN_INTERMEDIATE_NODE"); } 
    | TN_PRIMITIVE_IPIN TAB         { $$ = strdup("TN_PRIMITIVE_IPIN"); } 
    | TN_PRIMITIVE_OPIN TAB         { $$ = strdup("TN_PRIMITIVE_OPIN"); } 
    | TN_FF_IPIN TAB                { $$ = strdup("TN_FF_IPIN"); } 
    | TN_FF_OPIN TAB                { $$ = strdup("TN_FF_OPIN"); } 
    | TN_FF_SINK TAB                { $$ = strdup("TN_FF_SINK"); } 
    | TN_FF_SOURCE TAB              { $$ = strdup("TN_FF_SOURCE"); } 
    | TN_FF_CLOCK TAB               { $$ = strdup("TN_FF_CLOCK"); } 
    | TN_CLOCK_SOURCE TAB           { $$ = strdup("TN_CLOCK_SOURCE"); } 
    | TN_CLOCK_OPIN TAB             { $$ = strdup("TN_CLOCK_OPIN"); } 
    | TN_CONSTANT_GEN_SOURCE TAB    { $$ = strdup("TN_CONSTANT_GEN_SOURCE"); }
    ;

num_tnodes: NUM_TNODES int_number {$$ = $2; }
    ;

num_tnode_levels: NUM_TNODE_LEVELS int_number {$$ = $2; }
    ;

timing_graph_level: LEVEL int_number NUM_LEVEL_NODES int_number EOL {printf("Level: %d, Nodes: %d\n", $2, $4);}
    | timing_graph_level level_node_list {}
    ;

level_node_list: NODES {}
    | TAB int_number   {printf("    node: %d\n", $2);}
    | EOL
    ;

node_arr_req_time: int_number t_arr_req t_arr_req EOL {$$.node_id = $1; $$.T_arr = $2; $$.T_req = $3; printf("Node %d Arr_T: %e Req_T: %e\n", $1, $2, $3);}

t_arr_req: TAB number { $$ = $2; }
    | TAB TAB '-' { $$ = NAN; }
    ;


number: float_number { $$ = $1; }
    | int_number { $$ = $1; }
    ;

float_number: FLOAT_NUMBER { $$ = $1; }
    ;

int_number: INT_NUMBER { $$ = $1; }
    ;

%%


int yyerror(const char *msg) {
    printf("Line: %d, Text: '%s', Error: %s\n",yylineno, yytext, msg);
    return 1;
}
