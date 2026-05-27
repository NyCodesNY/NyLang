function Main() {
    System.Print("10 + 5 * 2 =");
    init mathPrecedence = 10 + 5 * 2;
    System.Print(mathPrecedence); // Should be 20

    System.Print("(10 + 5) * 2 =");
    init mathParens = (10 + 5) * 2;
    System.Print(mathParens); // Should be 30

    System.Print("100 / 2 - 5 =");
    init divSub = 100 / 2 - 5;
    System.Print(divSub); // Should be 45
}
