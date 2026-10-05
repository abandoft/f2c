subroutine findloc_logical_models(values,searched,back,first,last,narrow,short,normal)
  use iso_fortran_env, only: int8, int16, int64
  implicit none
  logical(int64), intent(in) :: values(3), searched, back
  integer(int64), intent(out) :: first, last
  integer(int8), intent(out) :: narrow
  integer(int16), intent(out) :: short
  integer, intent(out) :: normal
  first = findloc(values,searched,dim=1,kind=int64)
  last = findloc(values,searched,dim=1,kind=int64,back=back)
  narrow = findloc(values,searched,dim=1,kind=int8,back=back)
  short = findloc(values,searched,dim=1,kind=int16,back=back)
  normal = findloc(values,searched,dim=1,back=back)
end subroutine

subroutine findloc_real_models(values,searched,first,last)
  use iso_fortran_env, only: real64, int64
  implicit none
  real(real64), intent(in) :: values(4), searched
  integer(int64), intent(out) :: first, last
  first = findloc(values,searched,dim=1,kind=int64)
  last = findloc(values,searched,dim=1,kind=int64,back=.true.)
end subroutine

subroutine findloc_integer_real_model(values,searched,position)
  use iso_fortran_env, only: real32, int64
  implicit none
  integer(int64), intent(in) :: values(2)
  real(real32), intent(in) :: searched
  integer(int64), intent(out) :: position
  position = findloc(values,searched,dim=1,kind=int64)
end subroutine

subroutine findloc_dimension_model(values,dimension,position)
  use iso_fortran_env, only: int64
  implicit none
  integer, intent(in) :: values(3)
  integer(int64), intent(in) :: dimension
  integer(int64), intent(out) :: position
  position = findloc(values,2,dim=dimension,kind=int64)
end subroutine

subroutine findloc_mask_shape(n,m,p,q,values,mask,position)
  use iso_fortran_env, only: int64
  implicit none
  integer, intent(in) :: n,m,p,q
  integer, intent(in) :: values(n,m)
  logical, intent(in) :: mask(p,q)
  integer(int64), intent(out) :: position(2)
  position = findloc(values,2,mask=mask,kind=int64)
end subroutine

subroutine findloc_narrow_position(values,searched,position)
  use iso_fortran_env, only: int8, int64, real64
  implicit none
  integer, intent(in) :: values(128)
  real(real64), intent(in) :: searched
  integer(int64), intent(out) :: position
  position = findloc(values,searched,dim=1,kind=int8)
end subroutine
