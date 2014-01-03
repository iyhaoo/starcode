#include "trie.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))

// Module error reporting.
int ERROR;

// Array of nodes for the hits.
narray_t *HITS = NULL;

char COMMON[9] = {0,1,2,3,4,5,6,7,8};

// Local functions.
node_t *insert(node_t*, int, unsigned char);
void recursive_search(node_t*, const int*, int, int, const char,
      narray_t**, int, char);
void dash(node_t*, const int*);
void push(node_t*, narray_t**);
void init_miles(node_t*);
void destroy_nodes_downstream_of (node_t*, void(*)(void*));
int get_maxtau(node_t *root) { return ((info_t *)root->data)->maxtau; }
int get_bottom(node_t *root) { return ((info_t *)root->data)->bottom; }
int check_trie_error_and_reset(void);



// ------  SEARCH  ------ //


narray_t *
search
(
         node_t *trie,
   const char *query,
   const int tau,
         narray_t *hits,
   const int start,
   const int trail
)
{
   
   char maxtau = get_maxtau(trie);
   char bottom = get_bottom(trie);
   if (tau > maxtau) {
      ERROR = 44;
      // DETAIL: the nodes' cache has been allocated just enough
      // space to hold Levenshtein distances up to a maximum tau.
      // If this is exceeded, the search will try to read from and
      // write to forbidden memory space.
      fprintf(stderr, "requested tau greater than 'maxtau'\n");
      return hits;
   }

   int length = strlen(query);
   if (length > MAXBRCDLEN) {
      ERROR = 55;
      fprintf(stderr, "query longer than allowed max\n");
      return hits;
   }

   // Make sure the cache is allocated.
   info_t *info = (info_t *) trie->data;
   if (*info->miles == NULL) init_miles(trie);

   // Reset the miles that will be overwritten.
   for (int i = start+1 ; i <= trail ; i++) {
      info->miles[i]->pos = 0;
   }

   // Translate the query string. The first 'char' is kept to store
   // the length of the query, which shifts the array by 1 position.
   int translated[M];
   translated[0] = length;
   translated[length+1] = EOS;
   for (int i = max(0, start-maxtau) ; i < length ; i++) {
      translated[i+1] = altranslate[(int) query[i]];
   }

   HITS = hits;

   // Run recursive search from cached nodes.
   narray_t *miles = info->miles[start];
   for (int i = 0 ; i < miles->pos ; i++) {
      node_t *start_node = miles->nodes[i];
      recursive_search(start_node, translated, tau, start + 1,
            maxtau, info->miles, trail, bottom);
   }

   return HITS;

}


void
recursive_search
(
         node_t   *  restrict node,
   const int      *  restrict query,
   const int         tau,
   const int         depth,
   const char        maxtau,
         narray_t ** restrict miles,
   const int         trail,
   const char        bottom
)
{

   node_t *child;
   char *pcache = node->cache + maxtau + 1;

   unsigned char mindist = M;
   int maxa = min((depth-1), tau);

   unsigned char mmatch;
   unsigned char shift;

   // One branch of the angle is identical among all children.
   unsigned char cmindist = M;
   uint32_t path = node->path;
   for (int a = maxa ; a > 0 ; a--) {
      // Right side (need the path).
      mmatch = pcache[a] + ((path >> 4*(a-1) & 15) != query[depth]);
      shift = min(pcache[a-1], COMMON[a+1]) + 1;
      COMMON[a] = min(mmatch, shift);
      cmindist = min(COMMON[a], mindist);
   }

   for (int i = 0 ; i < 6 ; i++) {
      // Skip if current node has no child at this position.
      if ((child = node->child[i]) == NULL) continue;

      char *ccache = child->cache + maxtau + 1;
      memcpy(ccache, COMMON, maxtau*sizeof(char));

      mindist = cmindist;

      // Fill in an angle of the dynamic programming table.
      for (int a = maxa ; a > 0 ; a--) {
         // Left side.
         mmatch = pcache[-a] + (i != query[depth-a]);
         shift = min(pcache[1-a], ccache[-a-1]) + 1;
         ccache[-a] = min(mmatch, shift);
         mindist = min(ccache[-a], mindist);
      }
      // Center.
      mmatch = pcache[0] + (i != query[depth]);
      shift = min(ccache[-1], ccache[1]) + 1;
      ccache[0] = min(mmatch, shift);
      mindist = min(ccache[0], mindist);

      if (mindist > tau) return;

      // Cache nodes in 'miles' when trailing.
      if (depth <= trail) push(child, miles+depth);

      // In case the smallest Levenshtein distance is
      // equal to the maximum allowed distance, no more mismatches
      // and indels are allowed. We can shortcut by searching perfect
      // matches.
      if ((mindist == tau) && (depth > trail)) {
         dash(child, query+depth+1);
         continue;
      }

      if ((depth == bottom) && (ccache[0] <= tau)) {
         push(child, &HITS);
      }

      recursive_search(child, query, tau, depth+1, maxtau,
            miles, trail, bottom);

   }

}


void
dash
(
   node_t * restrict node,
   const int * restrict query
)
{
   int c;
   node_t *child;

   while ((c = *query++) != EOS) {
      if ((c > 4) || (child = node->child[c]) == NULL) return;
      node = child;
   }

   if (node->data != NULL) {
      push(node, &HITS);
   }
   return;
}


void
push
(
   node_t *node,
   narray_t **stack_addr
)
{
   narray_t *stack = *stack_addr;
   if (stack->pos >= stack->lim) {
      size_t newsize = 2 * sizeof(int) + 2*stack->lim * sizeof(node_t *);
      narray_t *ptr = realloc(stack, newsize);
      if (ptr == NULL) {
         ERROR = 175;
         return;
      }
      *stack_addr = stack = ptr;
      stack->lim *= 2;
      
   }
   stack->nodes[stack->pos++] = node;
}


// ------  TRIE CONSTRUCTION  ------ //


node_t *
insert_string
(
   node_t *trie,
   const char *string
)
// SYNOPSIS:                                                              
//   Helper function to construct tries. Insert a string from a ROOT      
//   node by multiple calls to 'insert'.                                  
//                                                                        
// RETURN:                                                                
//   The leaf node in case of succes, 'NULL' otherwise.                   
{

   // FIXME: inserting the empty string returns the root, which
   // means that there is a risk of overwriting the trie info.

   int i;

   int nchar = strlen(string);
   if (nchar > MAXBRCDLEN) {
      ERROR = 347;
      return NULL;
   }
   
   unsigned char maxtau = get_maxtau(trie);

   // Find existing path and append one node.
   node_t *node = trie;
   for (i = 0 ; i < nchar ; i++) {
      node_t *child;
      int c = translate[(int) string[i]];
      if ((child = node->child[c]) == NULL) {
         node = insert(node, c, maxtau);
         break;
      }
      node = child;
   }

   // Append more nodes.
   for (i++ ; i < nchar ; i++) {
      if (node == NULL) {
         ERROR = 228;
         return NULL;
      }
      int c = translate[(int) string[i]];
      node = insert(node, c, maxtau);
   }

   return node;

}


node_t *
insert
(
            node_t * parent,
            int      position,
   unsigned char     maxtau
)
// SYNOPSIS:                                                              
//   Helper function to construct tries. Append a child to an existing    
//   node at a position specified by the character 'c'. NO CHECKING IS    
//   PERFORMED to make sure that this does not overwrite an existings     
//   node child (causing a memory leak) or that 'c' is an integer less    
//   than 5. Since 'insert' is called exclusiverly by 'insert_string'     
//   after a call to 'find_path', this checking is not required. If       
//   'insert' is called in another context, this check has to be          
//   performed.                                                           
//                                                                        
// RETURN:                                                                
//   The appended child node in case of success, 'NULL' otherwise.        
{
   // Initilalize child node.
   node_t *child = new_trienode(maxtau);
   if (child == NULL) {
      ERROR = 403;
      return NULL;
   }
   // Update child path and parent pointer.
   child->path = (parent->path << 4) + position;
   // Update parent node.
   parent->child[position] = child;
   return child;
}


// ------  CONSTRUCTORS and DESTRUCTORS  ------ //


narray_t *
new_narray
(void)
{
   narray_t *new = malloc(2 * sizeof(int) + 32 * sizeof(node_t *));
   if (new == NULL) {
      ERROR = 279;
      return NULL;
   }
   new->pos = 0;
   new->lim = 32;
   return new;
}


node_t *
new_trie
(
   unsigned char maxtau,
   unsigned char bottom
)
{

   if (maxtau > 8) {
      ERROR = 396;
      // DETAIL:                                                         
      // There is an absolute limit at 'tau' = 8 because the path is     
      // encoded as a 'char', ie an 8 x 2-bit array. It should be enough 
      // for most practical purposes.                                    
      return NULL;
   }

   node_t *root = new_trienode(maxtau);
   if (root == NULL) {
      ERROR = 406;
      return NULL;
   }

   info_t *info = malloc(sizeof(info_t));
   if (info == NULL) {
      ERROR = 412;
      return NULL;
   }

   memset(info->miles, 0, M * sizeof(narray_t *));
   info->maxtau = maxtau;
   info->bottom = bottom;
   root->data = info;
   return root;
}


node_t *
new_trienode
(
   unsigned char maxtau
)
// SYNOPSIS:                                                              
//   Constructor for a trie node or a  root.                              
//                                                                        
// RETURN:                                                                
//   A root node with no data and no children.                            
{
   size_t base = sizeof(node_t);
   size_t extra = (2*maxtau + 3) * sizeof(char);
   node_t *node = malloc(base + extra);
   if (node == NULL) {
      ERROR = 483;
      return NULL;
   }
   memset(node, 0, base);
   for (int i = 0 ; i < 2*maxtau + 3 ; i++) {
      node->cache[i] = (unsigned char) abs(i-1-maxtau);
   }
   return node;
}


void
destroy_trie
(
   node_t *trie,
   void (*destruct)(void *)
)
{
   // Free the node arrays in the cache.
   info_t *info = (info_t *) trie->data;
   for (int i = 0 ; i < M ; i++) {
      if (info->miles[i] != NULL) free(info->miles[i]);
   }
   // Set info to 'NULL' before recursive destruction.
   free(info);
   trie->data = NULL;
   // ... and bye-bye.
   destroy_nodes_downstream_of(trie, destruct);
}


void
destroy_nodes_downstream_of
(
   node_t *node,
   void (*destruct)(void *)
)
// SYNOPSIS:                                                              
//   Free the memory allocated on a trie.                                 
//                                                                        
// RETURN:                                                                
//   'void'.                                                              
{  
   if (node != NULL) {
      for (int i = 0 ; i < 5 ; i++) {
         destroy_nodes_downstream_of(node->child[i], destruct);
      }
      if (node->data != NULL && destruct != NULL) (*destruct)(node->data);
      free(node);
   }
}


// ------  MISCELLANEOUS ------ //

void init_miles
(
   node_t *trie
)
{
   info_t *info = (info_t *) trie->data;
   for (int i = 0 ; i < M ; i++) {
      info->miles[i] = new_narray();
      // TODO: You can do better than this!
      if (info->miles[i] == NULL) {
         exit (EXIT_FAILURE);
      }
   }
   // Push the root into the 0-depth cache.
   // It will be the only node ever in there.
   push(trie, info->miles);
}


int check_trie_error_and_reset(void) {
   if (ERROR) {
      int last_error_at_line = ERROR;
      ERROR = 0;
      return last_error_at_line;
   }
   return 0;
}


/* -- Notes, tests etc. --

// An unsuccessful attempt at computing min.
char minimum(char a, char b) {
   char scratch;
   __asm__ __volatile__ (
       "sub %0, %1 \n\t"
       "sbb %2, %2 \n\t"
       "and %1, %2 \n\t"
       "add %2, %0 \n\t"
   : "+r"(a)
   : "r"(b), "r"(scratch)
   : );
   return a;
}

*/

