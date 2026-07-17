program select_type_dispatch
  implicit none
  type :: base_record
    integer :: identifier
  end type base_record
  type, extends(base_record) :: weighted_record
    real :: weight
  end type weighted_record
  type(weighted_record) :: item

  item%identifier = 17
  item%weight = 2.5
  call inspect(item)

contains

  subroutine inspect(object)
    class(base_record), intent(in) :: object
    dynamic_dispatch: select type (object)
    type is (weighted_record) dynamic_dispatch
      if (object%identifier /= 17) stop 1
      nested_choice: select case (object%identifier)
      case (17) nested_choice
        if (object%weight /= 2.5) stop 2
      case default nested_choice
        stop 5
      end select nested_choice
      if (object%weight /= 2.5) stop 6
    class is (base_record) dynamic_dispatch
      stop 3
    class default dynamic_dispatch
      stop 4
    end select dynamic_dispatch
  end subroutine inspect

end program select_type_dispatch
