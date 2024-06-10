declare double @floor(double)

@seed = common global double 0.000000e+00
@a = common global double 0.000000e+00
@m = common global double 0.000000e+00
define double @randk() {
entry:
  %tmp = alloca double, align 8
  %a = load double, ptr @a, align 8
  %seed = load double, ptr @seed, align 8
  %mulres = fmul double %a, %seed
  store double %mulres, ptr %tmp, align 8
  %tmp1 = load double, ptr %tmp, align 8
  %m = load double, ptr @m, align 8
  %tmp2 = load double, ptr %tmp, align 8
  %m3 = load double, ptr @m, align 8
  %addres = fdiv double %tmp2, %m3
  %calltmp = call double @floor(double %addres)
  %mulres4 = fmul double %m, %calltmp
  %subres = fsub double %tmp1, %mulres4
  store double %subres, ptr @seed, align 8
  %seed5 = load double, ptr @seed, align 8
  %m6 = load double, ptr @m, align 8
  %addres7 = fdiv double %seed5, %m6
  ret double %addres7
}

define double @randinit(double %x) {
entry:
  %x1 = alloca double, align 8
  store double %x, ptr %x1, align 8
  store double 1.689700e+04, ptr @a, align 8
  store double 0x41DFFFFFFFC00000, ptr @m, align 8
  %x2 = load double, ptr %x1, align 8
  %m = load double, ptr @m, align 8
  %x3 = load double, ptr %x1, align 8
  %m4 = load double, ptr @m, align 8
  %addres = fdiv double %x3, %m4
  %calltmp = call double @floor(double %addres)
  %mulres = fmul double %m, %calltmp
  %subres = fsub double %x2, %mulres
  store double %subres, ptr @seed, align 8
  ret double 0.000000e+00
}

