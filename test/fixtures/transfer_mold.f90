program transfer_mold
  implicit none
  real :: source(4)

  source = [1.0, 2.0, 3.0, 4.0]
  call consume(transfer(source(1:4), [(0.0, 0.0)], 2))

contains

  subroutine consume(values)
    complex, intent(in) :: values(*)

    if (abs(real(values(1)) - 1.0) > epsilon(1.0) .or. &
        abs(aimag(values(1)) - 2.0) > epsilon(1.0)) stop 1
    if (abs(real(values(2)) - 3.0) > epsilon(1.0) .or. &
        abs(aimag(values(2)) - 4.0) > epsilon(1.0)) stop 2
  end subroutine consume

end program transfer_mold
