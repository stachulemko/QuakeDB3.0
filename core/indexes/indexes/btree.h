//
// Created by stas on 12.05.2026.
//

#ifndef QUAKEDB3_0_NODEBTREE_H
#define QUAKEDB3_0_NODEBTREE_H
#include <stdlib.h>
#include <string.h>
#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include <stdio.h>
#include <time.h>

typedef struct type {
    AllVar val;
    int32_t blockId;
}type;


typedef struct Node {
    type* nodes[M];
    int32_t countNodes;
    struct Node *nodesNext[M];   // ← struct Node
    int32_t countNodesNext;
} Node;

int32_t getRandomInRange(int32_t min, int32_t max) {
    // We calculate the range (difference between max and min)
    // and add 1 to include the max value itself.
    int32_t range = max - min + 1;

    // Generate the random number within the range and shift it by min
    return (rand() % range) + min;
}


static inline void quickSort(type *a[], int32_t size) {
    if (size <= 1) return;

    int32_t pivotIndex = getRandomInRange(0, size - 1);
    type *tmp = a[pivotIndex];
    a[pivotIndex] = a[size - 1];
    a[size - 1] = tmp;

    type *pivot = a[size - 1];
    int32_t storeIndex = 0;

    for (int32_t i = 0; i < size - 1; i++) {
        if (all_var_cmp(&a[i]->val, &pivot->val) < 0) {
            type *t = a[i];
            a[i] = a[storeIndex];
            a[storeIndex] = t;
            storeIndex++;
        }
    }

    type *t = a[storeIndex];
    a[storeIndex] = a[size - 1];
    a[size - 1] = t;

    quickSort(a, storeIndex);
    quickSort(a + storeIndex + 1, size - storeIndex - 1);
}


static inline void createNodeM(Node **node) {
    *node = (Node *)malloc(sizeof(Node));
}

static inline void createNodeC(Node **node) {
    *node = (Node *)calloc(1, sizeof(Node));
}

void clearNodesTab(Node *node, int32_t size) {
    for (int i = 0; i < size; i++) {
        //free(node->nodes[i]);
        node->nodes[i] = NULL;
    }
}

static inline void addElement(Node *node, type val, int8_t operator, int8_t method, AllVar val2) {
    if (node->countNodes < M && node->countNodesNext == 0) {
        node->nodes[node->countNodes] = (type *)malloc(sizeof(type));
        if (node->nodes[node->countNodes]) {
            *node->nodes[node->countNodes] = val;
            node->countNodes++;
        }
        if (M == 4) {
            if (node->nodes[0] && node->nodes[1] && all_var_cmp(&node->nodes[0]->val, &node->nodes[1]->val) > 0) {
                type *tmp = node->nodes[0]; node->nodes[0] = node->nodes[1]; node->nodes[1] = tmp;
            }
            if (node->nodes[2] && node->nodes[3] && all_var_cmp(&node->nodes[2]->val, &node->nodes[3]->val) > 0) {
                type *tmp = node->nodes[2]; node->nodes[2] = node->nodes[3]; node->nodes[3] = tmp;
            }
            if (node->nodes[0] && node->nodes[2] && all_var_cmp(&node->nodes[0]->val, &node->nodes[2]->val) > 0) {
                type *tmp = node->nodes[0]; node->nodes[0] = node->nodes[2]; node->nodes[2] = tmp;
            }
            if (node->nodes[1] && node->nodes[3] && all_var_cmp(&node->nodes[1]->val, &node->nodes[3]->val) > 0) {
                type *tmp = node->nodes[1]; node->nodes[1] = node->nodes[3]; node->nodes[3] = tmp;
            }
            if (node->nodes[1] && node->nodes[2] && all_var_cmp(&node->nodes[1]->val, &node->nodes[2]->val) > 0) {
                type *tmp = node->nodes[1]; node->nodes[1] = node->nodes[2]; node->nodes[2] = tmp;
            }
        } else {
            quickSort(node->nodes, node->countNodes);
        }
    } else {
        if (node->countNodes >= M && node->countNodesNext == 0) {
            /* Ensure keys are sorted before split */
            quickSort(node->nodes, node->countNodes);

            int32_t mid = node->countNodes / 2;
            type *midEl = node->nodes[mid];
            Node* less;
            Node* more;

            createNodeC(&less);
            createNodeC(&more);

            for (int i = 0; i < node->countNodes; i++) {
                if (i == mid) continue;
                if (evaluateAllVar(&node->nodes[i]->val, &midEl->val, 2, method) == 1) {
                    less->nodes[less->countNodes] = node->nodes[i];
                    less->countNodes++;
                } else {
                    more->nodes[more->countNodes] = node->nodes[i];
                    more->countNodes++;
                }
            }

            if (node->countNodesNext > 0) {
                for (int j = 0; j < node->countNodesNext; j++) {
                    if (j <= mid) {
                        less->nodesNext[less->countNodesNext++] = node->nodesNext[j];
                    } else {
                        more->nodesNext[more->countNodesNext++] = node->nodesNext[j];
                    }
                }
            }

            if (evaluateAllVar(&val.val, &midEl->val, 2, method) == 1) {
                less->nodes[less->countNodes] = (type *)malloc(sizeof(type));
                *less->nodes[less->countNodes] = val;
                less->countNodes++;
            } else {
                more->nodes[more->countNodes] = (type *)malloc(sizeof(type));
                *more->nodes[more->countNodes] = val;
                more->countNodes++;
            }

            quickSort(less->nodes, less->countNodes);
            quickSort(more->nodes, more->countNodes);

            node->countNodes = 1;
            clearNodesTab(node, M);
            node->nodes[0] = midEl;
            node->countNodesNext = 2;
            node->nodesNext[0] = less;
            node->nodesNext[1] = more;

        } else {
            /* choose correct child to descend to
               Keys: node->nodes[0..n-1], children: nodesNext[0..n]
               if val < node->nodes[i] -> go to child i
               otherwise (>= all keys) -> go to child n
            */
            for (int i = 0; i < node->countNodes; ++i) {
                if (evaluateAllVar(&val.val, &node->nodes[i]->val, 2, method) == 1) {
                    /* ensure child exists */
                    if (node->nodesNext[i] == NULL) createNodeC(&node->nodesNext[i]);
                    addElement(node->nodesNext[i], val, operator, method, val2);
                    return;
                }
            }
            /* greater or equal than all keys -> last child */
            if (node->nodesNext[node->countNodes] == NULL) createNodeC(&node->nodesNext[node->countNodes]);
            addElement(node->nodesNext[node->countNodes], val, operator, method, val2);
            return;
        }
    }

}


int32_t getBlock(Node *root,AllVar val,int8_t operator, int8_t method, AllVar val2) {
    if (root == NULL) return -1;
    int32_t index = 0;
    int32_t tabSize = 0;
    for (int i=0;i<root->countNodes;i++) {
        if (evaluateAllVar(&val, &root->nodes[i]->val, operator, method) == 1) {
            if (operator == 0) {
                return root->nodes[i]->blockId;
            }

        }
        else {
            if (operator == 0) {
                if (evaluateAllVar(&val, &root->nodes[i]->val, 2, method) == 1) {
                    index = i;
                    tabSize++;

                }
            }

        }
    }
    if (operator == 0) {
        if (tabSize == 1) {
            getBlock(root->nodesNext[index], val, operator, method, val2);
        }
        else if (tabSize == 0 ) {
            if (evaluateAllVar(&val, &root->nodes[root->countNodes-1]->val, 2, method) == 0) {
                getBlock(root->nodesNext[root->countNodes], val, operator, method, val2);
            }
        }
    }
}


void printBtreeHelper(Node *root, int depth, int isLast) {
    if (root == NULL) return;

    for (int i = 0; i < depth; i++) {
        if (i == depth - 1) {
            printf(isLast ? "└── " : "├── ");
        } else {
            printf("│   ");
        }
    }

    printf("┌─ Node [");
    for (int i = 0; i < root->countNodes; i++) {
        if (root->nodes[i] != NULL) {
            if (root->nodes[i]->val.type == ID_INT32) {
                printf("%d (bid:%d)", root->nodes[i]->val.val.i32, root->nodes[i]->blockId);
            } else if (root->nodes[i]->val.type == ID_INT64) {
                printf("%ld (bid:%d)", root->nodes[i]->val.val.i64, root->nodes[i]->blockId);
            } else if (root->nodes[i]->val.type == ID_STRING) {
                printf("%s (bid:%d)", root->nodes[i]->val.val.str, root->nodes[i]->blockId);
            }
            if (i < root->countNodes - 1) printf(", ");
        }
    }
    printf("] ─┐\n");

    for (int i = 0; i < root->countNodesNext; i++) {
        if (root->nodesNext[i] != NULL) {
            printBtreeHelper(root->nodesNext[i], depth + 1, i == root->countNodesNext - 1);
        }
    }
}

void printBtree(Node *root) {
    if (root == NULL) {
        printf("Drzewo jest puste!\n");
        return;
    }
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║        STRUKTURA B-TREE               ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    printBtreeHelper(root, 0, 1);
    printf("\n");
}

/* Collect keys (as int64_t) in-order into `out` array up to max_count. Returns number of keys collected. */
static inline int collectKeysInOrder(Node *root, int64_t *out, int max_count) {
    if (root == NULL || out == NULL || max_count <= 0) return 0;
    int collected = 0;
    for (int i = 0; i < root->countNodes; ++i) {
        if (i < root->countNodesNext && root->nodesNext[i] != NULL) {
            collected += collectKeysInOrder(root->nodesNext[i], out + collected, max_count - collected);
            if (collected >= max_count) return collected;
        }
        if (root->nodes[i] != NULL && collected < max_count) {
            if (root->nodes[i]->val.type == ID_INT32) out[collected++] = root->nodes[i]->val.val.i32;
            else if (root->nodes[i]->val.type == ID_INT64) out[collected++] = root->nodes[i]->val.val.i64;
            else {
                /* unsupported type for numeric comparison: convert length or skip */
                out[collected++] = 0;
            }
        }
    }
    if (root->countNodes < root->countNodesNext && root->nodesNext[root->countNodes] != NULL && collected < max_count) {
        collected += collectKeysInOrder(root->nodesNext[root->countNodes], out + collected, max_count - collected);
    }
    return collected;
}

/* Verify in-order keys against expected array. Prints mismatch info. Returns 1 if equal, 0 otherwise. */
static inline int verifyInOrder(Node *root, const int64_t expected[], int expected_count) {
    if (expected == NULL) return 0;
    int64_t *collected = (int64_t *)malloc(sizeof(int64_t) * expected_count);
    if (!collected) return 0;
    int got = collectKeysInOrder(root, collected, expected_count + 1); // collect up to expected_count+1 to detect extra
    int ok = 1;
    if (got != expected_count) {
        printf("verifyInOrder: count mismatch: got=%d expected=%d\n", got, expected_count);
        ok = 0;
    }
    int cmp_len = (got < expected_count) ? got : expected_count;
    for (int i = 0; i < cmp_len; ++i) {
        if (collected[i] != expected[i]) {
            printf("verifyInOrder: mismatch at idx %d: got=%lld expected=%lld\n", i, (long long)collected[i], (long long)expected[i]);
            ok = 0;
        }
    }
    if (ok) printf("verifyInOrder: OK, all %d keys match and in order\n", expected_count);
    free(collected);
    return ok;
}


// =============================================================================================



typedef struct {
    int32_t pointerToPointerWithData;
    int32_t pointerToNextLabel;
} nodeMetaData;



typedef struct {
    int32_t pointerDataToNextPointerData;
    int32_t pointerToData;
}pointerToData;


void createBtree(FSMCache *fsmCacheBtree,
    FSMMapAll *fsmMapAllBtree,int32_t tableId,int32_t columnIndex) {
    uint8_t buffer[BLOCK_SIZE*3];
    int32_t offset = 0;
    // metaData only first
    marshal_int16(buffer + offset, ID_ROOT);
    offset += 2;
    marshal_int32(buffer + offset, 4);
    offset+=4;
    marshal_int32(buffer + offset,BLOCK_SIZE+offset+4);
    offset+=4;
    //marshal_int32(buffer + offset, BLOCK_SIZE*2+offset+4);
    //offset+=4;
    offset+=BLOCK_SIZE;
    nodeMetaData metaData ={-1,-1};
    metaData.pointerToPointerWithData = offset+4+BLOCK_SIZE;
    marshal_int32(buffer + offset, pointerToPointerWithData);
    offset+=4;
    marshal_int32(buffer + offset, metaData.pointerToNextLabel);
    offset+=4;
    offset+=BLOCK_SIZE;
    pointerToData ptd ={-1,-1};
    ptd.pointerToData = offset+4+BLOCK_SIZE;
    marshal_int32(buffer + offset, ptd.pointerDataToNextPointerData);
    offset+=4;
    marshal_int32(buffer + offset, ptd.pointerToData);
}



#endif //QUAKEDB3_0_NODEBTREE_H
