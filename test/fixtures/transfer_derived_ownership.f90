program transfer_derived_ownership
  implicit none

  type :: payload
    integer, allocatable :: values(:)
  end type payload

  type(payload) :: source
  type(payload) :: target
  type(payload) :: function_target
  integer :: owned_integer_bits
  character(len=1) :: owned_character_bits
  type(payload), allocatable :: source_array(:)
  type(payload), allocatable :: target_array(:)

  allocate(source%values(2))
  source%values = [7, 11]
  target = transfer(source, target)
  call consume(transfer(source, target))
  source%values(1) = 99
  if (.not. allocated(target%values)) stop 1
  if (any(target%values /= [7, 11])) stop 2
  deallocate(source%values)
  if (any(target%values /= [7, 11])) stop 3

  function_target = transfer(make_payload(), function_target)
  if (.not. allocated(function_target%values)) stop 4
  if (any(function_target%values /= [19, 23])) stop 5
  function_target = transfer(merge(make_payload(), target, .true.), function_target)
  if (any(function_target%values /= [19, 23])) stop 12
  function_target = transfer(merge(make_payload(), target, .false.), function_target)
  if (any(function_target%values /= [7, 11])) stop 13
  owned_integer_bits = transfer(make_payload(), owned_integer_bits)
  owned_character_bits = transfer(make_payload(), owned_character_bits)

  target_array = transfer(merge(make_payload(), target, .true.), [target])
  if (size(target_array) /= 1) stop 14
  if (.not. allocated(target_array(1)%values)) stop 15
  if (any(target_array(1)%values /= [19, 23])) stop 16

  allocate(source_array(1))
  allocate(source_array(1)%values(2))
  source_array(1)%values = [13, 17]
  target_array = transfer(source_array, source_array)
  source_array(1)%values(1) = 101
  if (size(target_array) /= 1) stop 6
  if (.not. allocated(target_array(1)%values)) stop 7
  if (any(target_array(1)%values /= [13, 17])) stop 8
  deallocate(source_array)
  if (any(target_array(1)%values /= [13, 17])) stop 9

contains

  subroutine consume(value)
    type(payload), intent(in) :: value
    if (.not. allocated(value%values)) stop 10
    if (any(value%values /= [7, 11])) stop 11
  end subroutine consume

  function make_payload() result(value)
    type(payload) :: value
    allocate(value%values(2))
    value%values = [19, 23]
  end function make_payload

end program transfer_derived_ownership
