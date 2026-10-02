program scalar_component_actuals
  implicit none
  type :: record_t
    integer(8) :: count
    real(8) :: weight
    complex(8) :: phase
    logical :: enabled
  end type
  type(record_t) :: data(2)
  integer :: answer

  data%count = cmplx(3000000000.0_8, 0.0_8, kind=8)
  if (any(data%count /= 3000000000_8)) stop 6
  data%phase = 3.0_8
  if (any(abs(data%phase - cmplx(3.0_8, 0.0_8, kind=8)) > 0.0_8)) stop 7
  data%phase = [cmplx(2.0, 3.0), cmplx(4.0, 5.0)]
  if (abs(data(1)%phase - cmplx(2.0_8, 3.0_8, kind=8)) > 0.0_8) stop 8
  if (abs(data(2)%phase - cmplx(4.0_8, 5.0_8, kind=8)) > 0.0_8) stop 9
  data%phase = data(2:1:-1)%phase
  if (abs(data(1)%phase - cmplx(4.0_8, 5.0_8, kind=8)) > 0.0_8) stop 10
  if (abs(data(2)%phase - cmplx(2.0_8, 3.0_8, kind=8)) > 0.0_8) stop 11
  data(2:1)%phase = cmplx(9.0, 9.0)
  if (abs(data(1)%phase - cmplx(4.0_8, 5.0_8, kind=8)) > 0.0_8) stop 12
  data%count = 10_8
  data%weight = 2.0_8
  data%phase = cmplx(1.0_8, 2.0_8, kind=8)
  data%enabled = .false.
  call edit(data(2)%count, data(2)%weight, data(2)%phase, data(2)%enabled)
  answer = edit_result(data(1)%count, data(1)%weight, data(1)%phase, data(1)%enabled)
  if (answer /= 11) stop 1
  if (any(data%count /= 11_8)) stop 2
  if (any(abs(data%weight - 4.0_8) > 0.0_8)) stop 3
  if (any(abs(data%phase - cmplx(2.0_8, 4.0_8, kind=8)) > 0.0_8)) stop 4
  if (.not. all(data%enabled)) stop 5
  print *, 'scalar component actuals passed'

contains

  subroutine edit(count, weight, phase, enabled)
    integer(8), intent(inout) :: count
    real(8), intent(inout) :: weight
    complex(8), intent(inout) :: phase
    logical, intent(inout) :: enabled
    count = count + 1_8
    weight = weight * 2.0_8
    phase = phase * 2.0_8
    enabled = .not. enabled
  end subroutine

  integer function edit_result(count, weight, phase, enabled)
    integer(8), intent(inout) :: count
    real(8), intent(inout) :: weight
    complex(8), intent(inout) :: phase
    logical, intent(inout) :: enabled
    call edit(count, weight, phase, enabled)
    edit_result = int(count)
  end function
end program
