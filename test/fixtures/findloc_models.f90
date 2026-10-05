program findloc_models
  use iso_fortran_env, only: int8, int16, int64, real32, real64
  implicit none
  integer, parameter :: ir(1) = findloc([1,2,2],2.0_real64)
  integer, parameter :: ri(1) = findloc([1.0_real32,2.0_real32],2_int64)
  integer, parameter :: cr(1) = findloc([(2.0_real32,1.0_real32),(2.0_real32,0.0_real32)],2.0_real64)
  integer, parameter :: ll(1) = findloc([.false._int8,.true._int8],.true._int64)
  integer, parameter :: exact(1) = findloc([9007199254740992_int64,9007199254740993_int64], &
                                        9007199254740993_int64)
  integer, parameter :: rounded(1) = findloc([16777216.0_real32],16777217_int64)
  integer, parameter :: distinct(1) = findloc([16777216.0_real64],16777217_int64)
  integer, parameter :: direct(1) = findloc([4611686018427387904.0_real32, &
                                          4611686568183201792.0_real32],4611686293305294849_int64)
  integer(int64), parameter :: scalar = findloc([1,2,2],2.0_real64,dim=1,back=.true.,kind=int64)
  integer(int16), parameter :: words(1) = findloc(['AA ','BB '],'AA',kind=int16)
  integer, parameter :: empty(0,2) = reshape([1],[0,2])
  integer, parameter :: no_matches(2) = findloc(empty,1.0_real64,dim=1)
  if (any(ir /= [2]) .or. any(ri /= [2]) .or. any(cr /= [2]) .or. any(ll /= [2])) stop 1
  if (any(exact /= [2]) .or. any(rounded /= [1]) .or. any(distinct /= [0])) stop 2
  if (any(direct /= [2]) .or. scalar /= 3_int64 .or. any(words /= [1])) stop 3
  if (any(no_matches /= [0,0])) stop 4
  print '(A)', 'FINDLOC independent constant model contracts passed'
end program
