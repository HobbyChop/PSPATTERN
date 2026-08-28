#ifndef KCALL_LIB_H
#define KCALL_LIB_H
#ifdef __cplusplus
extern "C" {
#endif
  #define _F(_1,_2,_3,NAME,...) NAME
  #define kcall(...) _F(__VA_ARGS__, kcall_3, kcall_2, ~)(__VA_ARGS__)
  typedef int (*FCall)(void);
  typedef int (*FPCall)(void* const);
  int kcall_2(FCall const func, const unsigned int seg);
  int kcall_3(FPCall const func, const unsigned int seg, void* const param);
  int kinit(const void* const handler);
#ifdef __cplusplus
}
#endif
#endif
