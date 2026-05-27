function int add(int a, int b) {
    return a + b;
}

function int multiply(int x, int y) {
    return x * y;
}

function void Main() {
    int a = 10;
    int b = 5;
    
    int sum = add(a, b);
    System.Print("Sum is:");
    System.Print(sum);
    
    int product = multiply(a, b);
    System.Print("Product is:");
    System.Print(product);
    
    System.Print("Nested call:");
    System.Print(multiply(add(2, 3), 4));
}
