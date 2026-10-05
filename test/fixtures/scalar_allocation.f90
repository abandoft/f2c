program scalar_allocation
  use iso_fortran_env, only: int8, int64, real64
  implicit none
  type :: entry
    integer(int64), allocatable :: value
    logical(int8), allocatable :: enabled
  end type
  type(entry) :: entries(2)
  integer(int64), allocatable, target :: value
  integer(int64), pointer :: alias
  real(real64), allocatable :: real_value
  complex(real64), allocatable :: complex_value
  logical(int8), allocatable :: logical_value
  integer(int64), allocatable, volatile :: qualified
  integer :: calls, selected_index
  value = 4294967297_int64
  if (.not. allocated(value)) stop 1
  alias => value
  value = value + 2_int64
  if (.not. associated(alias,value) .or. alias /= 4294967299_int64) stop 2
  real_value = 7_int64
  complex_value = 9_int64
  logical_value = .true.
  if (.not. allocated(real_value) .or. .not. allocated(complex_value)) stop 3
  if (abs(real_value - 7.0_real64) > tiny(1.0_real64)) stop 4
  if (abs(complex_value - (9.0_real64,0.0_real64)) > tiny(1.0_real64)) stop 5
  if (.not. allocated(logical_value) .or. .not. logical_value) stop 6
  qualified = 11_int64
  if (qualified /= 11_int64) stop 7
  calls = 0
  ! GNU 16 reevaluates this owner three times during allocation. Its paired
  ! profile captures the owner explicitly; f2c must preserve one evaluation.
#ifdef F2C_NATIVE_ORACLE_PROFILE
  selected_index = selected_entry()
  entries(selected_index)%value = 13_int64
#else
  entries(selected_entry())%value = 13_int64
  selected_index = 2
#endif
  if (calls /= 1 .or. .not. allocated(entries(2)%value)) stop 8
  if (selected_index /= 2) stop 14
  if (entries(2)%value /= 13_int64) stop 9
  entries(2)%enabled = .true.
  if (.not. allocated(entries(2)%enabled) .or. .not. entries(2)%enabled) stop 10
  nullify(alias)
  call replace(value)
  if (.not. allocated(value) .or. value /= 17_int64) stop 11
  value = produced()
  if (value /= 19_int64) stop 12
  deallocate(value,real_value,complex_value,logical_value,qualified)
  deallocate(entries(2)%value,entries(2)%enabled)
  print '(A)', 'intrinsic scalar allocation and storage identity passed'
contains
  integer function selected_entry()
    calls = calls + 1
    selected_entry = 2
  end function
  subroutine replace(destination)
    integer(int64), allocatable, intent(out) :: destination
    if (allocated(destination)) stop 13
    destination = 17_int64
  end subroutine
  function produced() result(destination)
    integer(int64), allocatable :: destination
    destination = 19_int64
  end function
end program
