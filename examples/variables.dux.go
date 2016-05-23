Str name = "Matheus Santos";
Int age = 33;

assert_equal(name+age, 'Matheus Santos33');

println('Hi, my name is {0} and I\'m {1} years old', name);

Int a = 1,
    b = 2,
    total = 0;

total = a + b;

assert_equal(total, 3);

total++;

assert_equal(total, 4);

total--;

assert_equal(total, 3);

println(total);
