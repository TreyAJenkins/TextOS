#ifndef _NUCLEUS_H
#define _NUCLEUS_H

bool FuseBlown(void);
int RequestToken(void);
bool ValidateToken(int token);

void SetSeed(int token, unsigned long seed);
int crand(int max);

int GenerateToken(void);
int InitSSP(void);

#endif
