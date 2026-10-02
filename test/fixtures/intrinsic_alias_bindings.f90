! F2018 15.5.5.2 paragraph 3 and note 1 permit renamed intrinsic references.
! GNU 16.1 omits these generic intrinsic exports; use an independent contract.
module intrinsic_alias_provider
  implicit none
  intrinsic :: max, min, sqrt, conjg, dsqrt
  public :: max, min, sqrt, conjg, dsqrt
end module
module intrinsic_alias_reexport
  use intrinsic_alias_provider, only: root => sqrt, double_root => dsqrt
  implicit none
end module
program intrinsic_alias_bindings
  use intrinsic_alias_provider, only: biggest => max, smallest => min, mirror => conjg
  use intrinsic_alias_reexport, only: square_root => root, wide_root => double_root
  implicit none
  if (biggest(2, 5) /= 5 .or. smallest(2, 5) /= 2) stop 1
  if (square_root(9.0) < 2.99 .or. square_root(9.0) > 3.01) stop 2
  if (real(mirror((3.0, 4.0))) < 2.99) stop 3
  if (aimag(mirror((3.0, 4.0))) > -3.99) stop 4
  if (wide_root(16.0d0) < 3.99d0 .or. wide_root(16.0d0) > 4.01d0) stop 5
  print '(a)', 'intrinsic alias contracts passed'
end program
