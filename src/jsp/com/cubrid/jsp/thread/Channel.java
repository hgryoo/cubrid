import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

public class Channel {
	private static final int MAX_REQUEST = 100;
    private Queue<Task> requestQueue;

	private final Worker[] threadPool;

	public Channel(int threads) {
		this.requestQueue = new ConcurrentLinkedQueue<Task>();

		threadPool = new Worker[threads];
		for (int i = 0; i < threadPool.length; i++) {
			threadPool[i] = new Worker("Worker-" + i, this);
		}
	}

	public void startWorkers() {
		for (int i = 0; i < threadPool.length; i++) {
			threadPool[i].start();
		}
	}

	public synchronized void putRequest(Task request) {
        /*
		while (count >= requestQueue.length) {
			try {
				wait();
			} catch (InterruptedException e) {
			}
		}
        */

        requestQueue.add (request);
		notifyAll();
	}

	public synchronized Task takeRequest() {
        /*
		while (count <= 0) {
			try {
				wait();
			} catch (InterruptedException e) {
			}
		}
        */
        
        while (requestQueue.size() <= 0) {
			try {
				wait();
			} catch (InterruptedException e) {
			}
        }

		Task request = requestQueue.poll ();
		notifyAll();
		return request;
	}
}