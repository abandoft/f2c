module intrinsic_result_model
  implicit none
  integer, parameter :: wide_kind = 8
  interface choose
    module procedure choose_i1, choose_i2, choose_i4, choose_i8, choose_l1, choose_l8
  end interface
contains
  integer function choose_i1(value)
    integer(kind=1), intent(in) :: value
    choose_i1 = 10 + int(value)
  end function
  integer function choose_i2(value)
    integer(kind=2), intent(in) :: value
    choose_i2 = 20 + int(value)
  end function
  integer function choose_i4(value)
    integer(kind=4), intent(in) :: value
    choose_i4 = 40 + value
  end function
  integer function choose_i8(value)
    integer(kind=8), intent(in) :: value
    choose_i8 = 80 + int(value)
  end function
  integer function choose_l1(value)
    logical(kind=1), intent(in) :: value
    choose_l1 = 101
    if (.not. value) choose_l1 = -101
  end function
  integer function choose_l8(value)
    logical(kind=8), intent(in) :: value
    choose_l8 = 108
    if (.not. value) choose_l8 = -108
  end function
end module

subroutine intrinsic_result_probe(wide, narrow, mask, models, choices)
  use intrinsic_result_model
  implicit none
  integer(kind=8), intent(in) :: wide
  integer(kind=1), intent(in) :: narrow(2,2)
  logical(kind=8), intent(in) :: mask(2,2)
  integer, intent(out) :: models(14), choices(6)
  models = [kind(digits(wide)), kind(range(wide)), kind(radix(wide)), &
    kind(bit_size(wide)), kind(matmul(narrow,narrow)), &
    kind(dot_product(narrow(:,1), narrow(:,2))), kind(all(mask)), kind(any(mask)), &
    kind(count(mask)), kind(logical(mask)), &
    kind(real(cmplx(1.0_8,2.0_8,kind=wide_kind))), kind(cmplx(1_8)), &
    kind(findloc(narrow, 1, kind=wide_kind, back=.true.)), &
    kind(shape(narrow, kind=wide_kind/4))]
  choices(1) = choose(digits(wide))
  choices(2) = choose(bit_size(wide))
  choices(3) = choose(dot_product(narrow(:,1), narrow(:,2)))
  choices(4) = choose(all(mask))
  choices(5) = choose(any(logical(mask,kind=1)))
  choices(6) = choose(count(mask,kind=wide_kind/4))
end subroutine
