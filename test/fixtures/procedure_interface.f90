subroutine procedure_interface_matrix(results)
  implicit none(type, external)
  integer, intent(out) :: results(10)
  integer :: values(3)
  character(4) :: text

  abstract interface
    integer function scalar_operation(value, bias) result(answer)
      implicit none
      integer, intent(in) :: value
      integer, intent(in), optional :: bias
    end function scalar_operation

    subroutine vector_operation(values, factor)
      implicit none
      integer, intent(inout) :: values(3)
      integer, intent(in) :: factor
    end subroutine vector_operation

    subroutine text_operation(text)
      implicit none
      character(*), intent(inout) :: text
    end subroutine text_operation

    character(4) function label_operation(value) result(label)
      implicit none
      integer, intent(in) :: value
    end function label_operation

    subroutine scalar_executor(operation, value, output, bias)
      import :: scalar_operation
      procedure(scalar_operation) :: operation
      integer, intent(in) :: value
      integer, intent(out) :: output
      integer, intent(in), optional :: bias
    end subroutine scalar_executor
  end interface

  call apply_scalar(add_bias, 5, results(1))
  call apply_scalar(add_bias, 5, results(2), 4)
  call maybe_apply(value=5, output=results(3))
  call maybe_apply(add_bias, 5, results(4))

  values = [1, 2, 3]
  call apply_vector(scale_values, values, 3)
  results(5) = values(1)
  results(6) = values(2)
  results(7) = values(3)

  text = 'AB'
  call apply_text(rewrite_text, text)
  if (text == 'ZB  ') then
    results(8) = 1
  else
    results(8) = 0
  end if

  text = apply_label(make_label, 2)
  if (text == 'OK  ') then
    results(9) = 1
  else
    results(9) = 0
  end if

  call compare_readonly(values, values, results(10))
  call dispatch(apply_scalar, add_bias, 3, results(10), 2)

contains

  integer function add_bias(value, bias) result(answer)
    integer, intent(in) :: value
    integer, intent(in), optional :: bias

    if (present(bias)) then
      answer = value + bias
    else
      answer = value + 7
    end if
  end function add_bias

  subroutine apply_scalar(operation, value, output, bias)
    procedure(scalar_operation) :: operation
    integer, intent(in) :: value
    integer, intent(out) :: output
    integer, intent(in), optional :: bias

    if (present(bias)) then
      output = operation(value, bias=bias)
    else
      output = operation(value)
    end if
  end subroutine apply_scalar

  subroutine maybe_apply(operation, value, output)
    procedure(scalar_operation), optional :: operation
    integer, intent(in) :: value
    integer, intent(out) :: output

    if (present(operation)) then
      output = operation(value)
    else
      output = -1
    end if
  end subroutine maybe_apply

  subroutine scale_values(values, factor)
    integer, intent(inout) :: values(3)
    integer, intent(in) :: factor

    values(1) = values(1) * factor
    values(2) = values(2) * factor
    values(3) = values(3) * factor
  end subroutine scale_values

  subroutine apply_vector(operation, values, factor)
    procedure(vector_operation) :: operation
    integer, intent(inout) :: values(3)
    integer, intent(in) :: factor

    call operation(values, factor)
  end subroutine apply_vector

  subroutine compare_readonly(left, right, output)
    integer, intent(in) :: left(3), right(3)
    integer, intent(out) :: output

    if (left(1) == right(1)) output = 1
  end subroutine compare_readonly

  subroutine rewrite_text(text)
    character(*), intent(inout) :: text

    text(1:1) = 'Z'
  end subroutine rewrite_text

  subroutine apply_text(operation, text)
    procedure(text_operation) :: operation
    character(*), intent(inout) :: text

    call operation(text)
  end subroutine apply_text

  character(4) function make_label(value) result(label)
    integer, intent(in) :: value

    if (value == 2) then
      label = 'OK'
    else
      label = 'NO'
    end if
  end function make_label

  character(4) function apply_label(operation, value) result(label)
    procedure(label_operation) :: operation
    integer, intent(in) :: value

    label = operation(value)
  end function apply_label

  subroutine dispatch(executor, operation, value, output, bias)
    procedure(scalar_executor) :: executor
    procedure(scalar_operation) :: operation
    integer, intent(in) :: value
    integer, intent(out) :: output
    integer, intent(in), optional :: bias

    call executor(operation, value, output, bias)
  end subroutine dispatch
end subroutine procedure_interface_matrix
