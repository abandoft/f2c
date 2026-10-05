program findloc_runtime
  use iso_fortran_env, only: int8, int16, int64, real32, real64
  implicit none
  integer :: a(3) = [1,2,2]
  real(real32) :: b(2) = [1.0_real32,2.0_real32]
  complex(real64) :: c(2) = [(1.0_real64,0.0_real64),(2.0_real64,3.0_real64)]
  logical(int8) :: d(3) = [.false._int8,.true._int8,.true._int8]
  integer, target :: matrix(2,3)
  integer(int64), target :: destination(6)
  integer :: cube_input(2,2,2)
  logical, target :: mask(2,3)
  integer, pointer :: view(:,:)
  integer(int64), pointer :: result_view(:)
  logical, pointer :: mask_view(:,:)
  integer(int64), allocatable, target :: retained(:)
  integer(int64), pointer :: alias(:)
  integer(int64) :: wide(2), scalar, dimension_result(3)
  integer(int64), allocatable :: allocated_scalar
  integer(int64) :: cube_locations(2,2)
  integer(int8) :: narrow(3)
  real(real64) :: converted(2)
  character(len=3) :: words(3) = ['AA ','BB ','AA ']
  integer :: value_calls, back_calls, mask_calls, dimension_calls
  integer :: f2c_findloc_value
  integer, volatile :: observed(3)
  logical, volatile :: observed_mask(3)
  character(len=3), volatile :: observed_words(3)
  call numeric(a,b,c,d,2.0_real64,2_int64,.true._int64)
  matrix = reshape([1,2,2,4,2,6],[2,3])
  mask = reshape([.true.,.false.,.true.,.true.,.true.,.false.],[2,3])
  wide = findloc(matrix,2.0_real64,kind=int64)
  if (any(wide /= [2_int64,1_int64])) stop 3
  wide = findloc(matrix,2_int64,kind=int64,back=.true.)
  if (any(wide /= [1_int64,3_int64])) stop 4
  narrow = findloc(matrix,2.0_real64,dim=1,kind=int8,back=.true.)
  if (any(narrow /= [2_int8,1_int8,1_int8])) stop 5
  converted = findloc(matrix,2_int64,dim=2,back=.true.,kind=int64)
  if (any(abs(converted - [3.0_real64,1.0_real64]) > tiny(1.0_real64))) stop 6
  allocate(retained(0:1))
  alias => retained
  retained = findloc(matrix,2.0_real32,kind=int64)
  if (lbound(retained,1) /= 0 .or. .not. associated(alias,retained)) stop 7
  if (any(alias /= [2_int64,1_int64])) stop 8
  nullify(alias)
  deallocate(retained)
  view => matrix(2:1:-1,3:1:-1)
  mask_view => mask(2:1:-1,3:1:-1)
  ! GNU 16 BACK + negative-stride MASK incorrectly returns zeros. Its paired
  ! oracle uses equivalent dense values; f2c's ordinary profile retains the
  ! original strided operands and the independent expected [2,2] contract.
#ifdef F2C_NATIVE_ORACLE_PROFILE
  wide = findloc(reshape(view,[2,3]),2.0_real64,mask=reshape(mask_view,[2,3]),kind=int64,back=.true.)
#else
  wide = findloc(view,2.0_real64,mask=mask_view,kind=int64,back=.true.)
#endif
  if (any(wide /= [2_int64,2_int64])) stop 9
  destination = -99
  result_view => destination(6:2:-2)
  result_view = findloc(view,2.0_real64,dim=1,mask=mask_view,kind=int64)
  if (any(destination /= [-99,0,-99,2,-99,2])) stop 10
  cube_input = reshape([1,2,2,4,2,6,1,2],[2,2,2])
  cube_locations = findloc(cube_input,2.0_real64,dim=3,kind=int64)
  if (any(reshape(cube_locations,[4]) /= [2_int64,1_int64,1_int64,2_int64])) stop 16
  f2c_findloc_value = 2
  wide = findloc(matrix,f2c_findloc_value,kind=int64)
  if (any(wide /= [2_int64,1_int64])) stop 11
  call character_search(words,'AA')
  observed = a
  observed_mask = [.true.,.false.,.true.]
  observed_words = words
  scalar = findloc(observed,2.0_real64,dim=1,mask=observed_mask,kind=int64)
  if (scalar /= 3_int64) stop 22
  scalar = findloc(observed_words,'AA',dim=1,back=.true.,kind=int64)
  if (scalar /= 3_int64) stop 23
  value_calls = 0
  back_calls = 0
  mask_calls = 0
  dimension_calls = 0
  wide(1:1) = findloc(a,searched_value(),back=search_back(),mask=search_mask(),kind=int64)
  if (wide(1) /= 3 .or. value_calls /= 1 .or. back_calls /= 1 .or. mask_calls /= 1) stop 12
  scalar = 7_int64 + findloc(a,searched_value(),dim=1,back=search_back(),kind=int64)
  if (scalar /= 10 .or. value_calls /= 2 .or. back_calls /= 2) stop 13
  ! With rank one the DIM value is redundant and Fortran 10.1.7 permits
  ! eliding its evaluation. Here a rank-two result actually depends on DIM.
  dimension_result = findloc(matrix,searched_value(),dim=which_dimension(),back=search_back(),kind=int64)
  if (any(dimension_result /= [2_int64,1_int64,1_int64])) stop 17
  if (value_calls /= 3 .or. back_calls /= 3 .or. dimension_calls /= 1) stop 18
  allocated_scalar = findloc(a,2.0_real64,dim=1,kind=int64)
  if (.not. allocated(allocated_scalar)) stop 19
  if (allocated_scalar /= 2_int64) stop 20
  allocated_scalar = findloc(a,2.0_real64,dim=1,kind=int64,back=.true.)
  if (allocated_scalar /= 3_int64) stop 21
  deallocate(allocated_scalar)
  print '(A)', 'FINDLOC runtime models, shapes, strides and evaluation contracts passed'
contains
  subroutine numeric(ia,ra,ca,la,rv,iv,lv)
    integer, intent(in) :: ia(3)
    real(real32), intent(in) :: ra(2)
    complex(real64), intent(in) :: ca(2)
    logical(int8), intent(in) :: la(3)
    real(real64), intent(in) :: rv
    integer(int64), intent(in) :: iv
    logical(int64), intent(in) :: lv
    integer :: ir(1),ri(1),cr(1),ll(1)
    ir = findloc(ia,rv)
    ri = findloc(ra,iv)
    cr = findloc(ca,1.0_real32)
    ll = findloc(la,lv)
    if (any(ir /= [2]) .or. any(ri /= [2]) .or. any(cr /= [1]) .or. any(ll /= [2])) stop 1
    if (findloc(ia,rv,dim=1) /= 2 .or. findloc(la,lv,dim=1,back=.true.) /= 3) stop 2
  end subroutine
  subroutine character_search(values,searched)
    character(len=*), intent(in) :: values(:), searched
    integer(int16) :: position(1)
    position = findloc(values,searched,kind=int16,back=.true.)
    if (any(position /= [3_int16])) stop 14
    if (findloc(values,searched,dim=1,kind=int64) /= 1_int64) stop 15
  end subroutine
  real(real64) function searched_value()
    value_calls = value_calls + 1
    searched_value = 2.0_real64
  end function
  logical(int64) function search_back()
    back_calls = back_calls + 1
    search_back = .true._int64
  end function
  logical function search_mask()
    mask_calls = mask_calls + 1
    search_mask = .true.
  end function
  integer(int64) function which_dimension()
    dimension_calls = dimension_calls + 1
    which_dimension = 1_int64
  end function
end program
