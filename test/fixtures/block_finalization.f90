module block_finalization_state
  implicit none
  integer :: scalar_finalized = 0
  integer :: array_finalized = 0

  type :: tracked
    integer :: value = 0
  contains
    final :: finalize_scalar, finalize_array
  end type tracked
contains
  subroutine finalize_scalar(object)
    type(tracked), intent(inout) :: object
    scalar_finalized = scalar_finalized + 1
    object%value = 0
  end subroutine finalize_scalar

  subroutine finalize_array(objects)
    type(tracked), intent(inout) :: objects(:)
    array_finalized = array_finalized + 10
  end subroutine finalize_array

  subroutine normal_blocks()
    integer :: iteration
    do iteration = 1, 2
      block
        type(tracked) :: scalar
        type(tracked) :: fixed(2)
        type(tracked), allocatable :: dynamic(:)
        scalar%value = iteration
        fixed(1)%value = iteration
        allocate(dynamic(2))
        dynamic(1)%value = iteration
      end block
    end do
  end subroutine normal_blocks

  subroutine return_from_block()
    block
      type(tracked) :: value
      value%value = 10
      return
    end block
  end subroutine return_from_block

  subroutine cycle_from_block()
    integer :: iteration
    do iteration = 1, 2
      block
        type(tracked) :: value
        value%value = iteration
        if (iteration == 1) cycle
      end block
    end do
  end subroutine cycle_from_block

  subroutine exit_from_block()
    integer :: iteration
    do iteration = 1, 2
      block
        type(tracked) :: value
        value%value = iteration
        exit
      end block
    end do
  end subroutine exit_from_block

  subroutine goto_from_block()
    block
      type(tracked) :: value
      value%value = 1
      goto 100
    end block
100 continue
  end subroutine goto_from_block

  subroutine io_error_from_block()
    character(1) :: record
    integer :: value
    record = 'x'
    block
      type(tracked) :: object
      object%value = 1
      read(record, '(I1)', err=100) value
    end block
100 continue
  end subroutine io_error_from_block

end module block_finalization_state

program block_finalization
  use block_finalization_state
  implicit none

  call normal_blocks()
  if (scalar_finalized /= 2 .or. array_finalized /= 40) stop 1
  call return_from_block()
  if (scalar_finalized /= 3) stop 2
  call cycle_from_block()
  if (scalar_finalized /= 5) stop 3
  call exit_from_block()
  if (scalar_finalized /= 6) stop 4
  call goto_from_block()
  if (scalar_finalized /= 7) stop 5
  call io_error_from_block()
  if (scalar_finalized /= 8) stop 6
end program block_finalization
