0module pointer_state_seed
contains
  integer function rebind(p,t) result(total)
    integer, pointer, volatile, intent(inout) :: p(:)
    integer, target, intent(inout) :: t(:)
    p(-2:) => t(6:2:-2)
    total = sum(p)
  end function
  subroutine observe(p,t)
    integer, pointer, volatile, intent(inout) :: p(:)
    integer, target, intent(inout) :: t(:)
    integer :: total
    total = rebind(p,t)
    if (total /= 120 .or. lbound(p,1) /= -2) stop 1
    if (p(0) /= 20) stop 2
    nullify(p)
    if (associated(p)) stop 3
  end subroutine
end module
program pointer_state_main
  use pointer_state_seed
  integer, target :: t(6) = [10,20,30,40,50,60]
  integer, pointer :: p(:)
  p => t
  call observe(p,t)
end program
