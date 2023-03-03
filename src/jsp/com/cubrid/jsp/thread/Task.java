import java.util.Random;

public abstract class Task {
	private final String name;
	private final int number;

	public Task(String name, int number) {
		this.name = name;
		this.number = number;
	}

	public void execute() {
        // empty
	}

	public String toString() {
		return "[ Request from " + name + " No." + number + " ]";
	}
}