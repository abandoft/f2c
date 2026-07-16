subroutine explicit_interface_matrix(results)
  implicit none(type, external)
  integer, intent(out) :: results(9)
  integer :: value
  real :: values(3)
  real :: score_value
  character(4) :: tag

  interface
    subroutine adjust(value, scale, values, tag)
      implicit none
      integer, intent(inout) :: value
      real, intent(in), optional :: scale
      real, intent(inout) :: values(3)
      character(*), intent(in) :: tag
    end subroutine adjust
  end interface

  interface score_generic
    real function score_impl(values, offset, bias) result(total)
      implicit none
      real, intent(in) :: values(3)
      real, intent(in), optional :: offset
      real, intent(in) :: bias
    end function score_impl
  end interface score_generic

  interface classify
    real function classify_integer(value)
      implicit none
      integer, intent(in) :: value
    end function classify_integer
    real function classify_real(value)
      implicit none
      real, intent(in) :: value
    end function classify_real
  end interface classify

  value = 4
  values = [1.0, 2.0, 3.0]
  tag = 'AB'
  call adjust(value, values=values, tag=tag)
  score_value = score_generic(values, bias=2.5)

  results(1) = value
  results(2) = int(values(1))
  results(3) = int(values(2))
  results(4) = int(values(3))
  results(5) = int(score_value * 2.0)
  results(6) = int(values(1) + values(2) + values(3))
  results(7) = int(classify(value))
  results(8) = int(classify(values(1)))
  if (tag == 'AB  ') then
    results(9) = 1
  else
    results(9) = 0
  end if
end subroutine explicit_interface_matrix
