package org.wyrmvr.wyrm;

public class MainActivity extends android.app.NativeActivity
{
	static
	{
		System.loadLibrary("wyrm");
	}

	@Override
	public native void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults);

	@Override
	public native void onNewIntent(android.content.Intent intent);

	@Override
	public native void onActivityResult(int requestCode, int resultCode, android.content.Intent data);

	private android.content.BroadcastReceiver BatteryInfoReceiver = null;
	public android.net.ConnectivityManager.NetworkCallback netcb = null;
	private android.net.ConnectivityManager.NetworkCallback allNetworksCallback = null;

	@Override
	protected void onCreate(android.os.Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);

		this.BatteryInfoReceiver = new BroadcastReceiver();
		this.netcb = new org.wyrmvr.wyrm.NetworkInfoCallback();
		this.allNetworksCallback = new org.wyrmvr.wyrm.NetworkInfoCallback();
		android.net.ConnectivityManager connectivity = (android.net.ConnectivityManager)getSystemService(android.content.Context.CONNECTIVITY_SERVICE);
		android.net.NetworkRequest request = new android.net.NetworkRequest.Builder()
		        .clearCapabilities()
		        .build();
		connectivity.registerNetworkCallback(request, this.allNetworksCallback);

		this.registerReceiver(this.BatteryInfoReceiver, new android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED));
	}

	@Override
	protected void onDestroy()
	{
		android.net.ConnectivityManager connectivity = (android.net.ConnectivityManager)getSystemService(android.content.Context.CONNECTIVITY_SERVICE);
		if (this.allNetworksCallback != null)
			connectivity.unregisterNetworkCallback(this.allNetworksCallback);
		if (this.BatteryInfoReceiver != null)
			unregisterReceiver(this.BatteryInfoReceiver);
		super.onDestroy();
	}
}
